#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;
class QTextEdit;
class StorageManager;

// Profiles mode main widget: list of profiles + actions
class ProfilesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProfilesWidget(StorageManager &storageManager, QWidget *parent = nullptr);

signals:
    void requestNavigateToModels();

private slots:
    void refreshProfiles();
    void createProfile();
    void editSelectedProfile();
    void duplicateSelectedProfile();
    void deleteSelectedProfile();
    void applySelectedProfile();
    void onSelectionChanged();
    void compareProfiles();

private:
    void setupUi();
    QString selectedProfileId() const;
    void updatePreview();

    StorageManager &m_storageManager;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_browseModelsButton = nullptr;
    QPushButton *m_compareButton = nullptr;
    QTextEdit *m_previewEdit = nullptr;
};
