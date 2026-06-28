#pragma once

#include <QMainWindow>

#include "storage/StorageManager.h"

class QMenu;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private:
    void createMenus();

    StorageManager m_storageManager;
    QTabWidget *m_tabWidget = nullptr;
    QMenu *m_teamsMenu = nullptr;
};
