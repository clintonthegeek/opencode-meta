#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;
class StorageManager;

// Simple Templates mode main widget: list of templates + actions
class TemplatesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TemplatesWidget(StorageManager &storageManager, QWidget *parent = nullptr);

private slots:
    void refreshTemplates();
    void createTemplate();
    void editSelectedTemplate();
    void duplicateSelectedTemplate();
    void deleteSelectedTemplate();
    void exportSelectedTemplate();
    void importTemplate();

private:
    void setupUi();
    QString selectedTemplateId() const;

    StorageManager &m_storageManager;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_importButton = nullptr;
};
