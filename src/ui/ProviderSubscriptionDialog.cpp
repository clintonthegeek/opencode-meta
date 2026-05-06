#include "ui/ProviderSubscriptionDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "storage/StorageManager.h"

ProviderSubscriptionDialog::ProviderSubscriptionDialog(StorageManager &storageManager,
                                                       const QStringList &allProviders,
                                                       QWidget *parent)
    : QDialog(parent)
    , m_storageManager(storageManager)
    , m_allProviders(allProviders)
{
    setWindowTitle(tr("Manage Provider Subscriptions"));
    setMinimumSize(400, 500);
    setupUi();
    populateProviderList(allProviders);
}

void ProviderSubscriptionDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Search filter
    auto *searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel(tr("Search providers:"), this));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Type to filter..."));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ProviderSubscriptionDialog::applySearchFilter);
    searchLayout->addWidget(m_searchEdit);
    mainLayout->addLayout(searchLayout);

    // Provider list
    m_listView = new QListView(this);
    m_model = new QStandardItemModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_listView->setModel(m_proxyModel);
    mainLayout->addWidget(m_listView, 1);

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    m_selectAllButton = new QPushButton(tr("Select All"), this);
    connect(m_selectAllButton, &QPushButton::clicked, this, &ProviderSubscriptionDialog::selectAllProviders);
    m_deselectAllButton = new QPushButton(tr("Deselect All"), this);
    connect(m_deselectAllButton, &QPushButton::clicked, this, &ProviderSubscriptionDialog::deselectAllProviders);
    buttonLayout->addWidget(m_selectAllButton);
    buttonLayout->addWidget(m_deselectAllButton);
    buttonLayout->addStretch(1);

    auto *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(dialogButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonLayout->addWidget(dialogButtons);
    mainLayout->addLayout(buttonLayout);
}

void ProviderSubscriptionDialog::populateProviderList(const QStringList &providers)
{
    const QSet<QString> preferred = m_storageManager.loadPreferredProviders();

    for (const QString &provider : providers) {
        auto *item = new QStandardItem(provider);
        item->setCheckable(true);
        item->setCheckState(preferred.contains(provider) ? Qt::Checked : Qt::Unchecked);
        item->setData(provider, Qt::UserRole);  // Store name for retrieval
        m_model->appendRow(item);
    }

    // Sort alphabetically
    m_model->sort(0);
}

void ProviderSubscriptionDialog::applySearchFilter(const QString &text)
{
    m_proxyModel->setFilterRegularExpression(text);
}

void ProviderSubscriptionDialog::selectAllProviders()
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *item = m_model->item(row);
        if (item) {
            item->setCheckState(Qt::Checked);
        }
    }
}

void ProviderSubscriptionDialog::deselectAllProviders()
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *item = m_model->item(row);
        if (item) {
            item->setCheckState(Qt::Unchecked);
        }
    }
}

QSet<QString> ProviderSubscriptionDialog::selectedProviders() const
{
    QSet<QString> selected;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *item = m_model->item(row);
        if (item && item->checkState() == Qt::Checked) {
            selected.insert(item->data(Qt::UserRole).toString());
        }
    }
    return selected;
}
