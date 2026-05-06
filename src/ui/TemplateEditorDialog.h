#pragma once

#include <QDialog>

class QTableWidget;
class QPushButton;

class Template;

// Simple editor for a single Template's agent list
class TemplateEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TemplateEditorDialog(const Template &t, QWidget *parent = nullptr);

    Template templateData() const;

private slots:
    void addAgentRow();
    void removeSelectedRow();

private:
    void setupUi();
    void loadFromTemplate(const Template &t);
    void applyToTemplate(Template &t) const;

    QTableWidget *m_table = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;

    Template *m_initialTemplate; // non-owning pointer used to cache initial data
};
