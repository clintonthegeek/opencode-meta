#pragma once

#include <QDialog>

class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class QLineEdit;
class QComboBox;

class Template;

// Simple editor for a single Template's agent list
class TemplateEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TemplateEditorDialog(const Template &t, QWidget *parent = nullptr);

    Template templateData() const;

    void accept() override;

private slots:
    void addAgentRow();
    void removeSelectedRow();
    void onTableItemChanged(QTableWidgetItem *item);

private:
    void setupUi();
    void loadFromTemplate(const Template &t);
    void applyToTemplate(Template &t) const;
    void rebuildDefaultAgentCombo(const QString &preferredSelection = QString());

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_versionEdit = nullptr;
    QComboBox *m_defaultAgentCombo = nullptr;

    QTableWidget *m_table = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;

    Template *m_initialTemplate; // non-owning pointer used to cache initial data
};
