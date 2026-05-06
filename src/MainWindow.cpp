#include "MainWindow.h"

#include <QLabel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OpenCode Meta Qt"));

    auto *label = new QLabel(QStringLiteral("OpenCode Meta Qt"), this);
    label->setAlignment(Qt::AlignCenter);

    // Simple central widget placeholder for now
    setCentralWidget(label);
}
