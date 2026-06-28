#pragma once

#include <QSortFilterProxyModel>
#include <QWidget>

class QLineEdit;

// FilterProxyModel: tiny QSortFilterProxyModel subclass that exposes
// the protected filterAcceptsRow() through a public acceptsRow() helper.
// The list widgets use acceptsRow() to drive QTableWidget::setRowHidden /
// QListWidgetItem::setHidden without swapping the underlying view model:
// keeping the widget as the view preserves existing item()/currentRow()/
// selectedRows()/currentItem() call sites across the project.
class FilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    bool acceptsRow(int sourceRow,
                    const QModelIndex &sourceParent = QModelIndex()) const;
};

// FilterBar: small reusable search/filter bar used by the list widgets.
//
// Provides:
//   * A QLineEdit with placeholder text (defaults to "Filter...").
//   * A built-in clear button (QLineEdit::setClearButtonEnabled).
//   * A widget-scoped ESC shortcut that clears the line edit.
//   * A filterChanged(QString) signal the host widget listens to.
//
// The widget itself does no filtering — it just emits the user's text so
// each host (Roles, Teams, Trials, Projects) can apply it to its own
// view via FilterProxyModel + row/item hiding.
class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(QWidget *parent = nullptr);
    explicit FilterBar(const QString &placeholderText, QWidget *parent = nullptr);

    QString filterText() const;
    void setFilterText(const QString &text);
    void clearFilter();

signals:
    void filterChanged(const QString &text);

private:
    QLineEdit *m_edit = nullptr;
};
