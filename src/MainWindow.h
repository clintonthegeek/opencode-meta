#pragma once

#include <QMainWindow>

#include "storage/StorageManager.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private:
    StorageManager m_storageManager;
};
