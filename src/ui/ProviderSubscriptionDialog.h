#pragma once

#include <QDialog>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

class QListView;
class QLineEdit;
class QPushButton;
class StorageManager;

/**
 * Modal dialog for managing subscribed providers in Models Browser.
 * Allows searching and toggling checkboxes for preferred providers.
 */
class ProviderSubscriptionDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * Constructor: Loads current preferred providers from storage.
     * \param storageManager Reference to shared storage.
     * \param allProviders Full list of available providers (for population).
     * \param parent Parent widget.
     */
    explicit ProviderSubscriptionDialog(StorageManager &storageManager,
                                        const QStringList &allProviders,
                                        QWidget *parent = nullptr);

    /**
     * Returns the set of selected (subscribed) providers after dialog closes.
     * \return QSet of provider names/IDs that are checked.
     */
    QSet<QString> selectedProviders() const;

private slots:
    void applySearchFilter(const QString &text);
    void selectAllProviders();
    void deselectAllProviders();

private:
    void setupUi();
    void populateProviderList(const QStringList &providers);

    StorageManager &m_storageManager;
    QStringList m_allProviders;
    QStandardItemModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxyModel = nullptr;
    QListView *m_listView = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_selectAllButton = nullptr;
    QPushButton *m_deselectAllButton = nullptr;
};
