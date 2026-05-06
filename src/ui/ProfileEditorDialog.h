#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QTableWidget;
class QPushButton;

class Template;
class Profile;

// Simple editor for a single Profile: base template + model assignments
class ProfileEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProfileEditorDialog(const QList<Template> &templates,
                                 const Profile &profile,
                                 const QString &defaultModelId,
                                 QWidget *parent = nullptr);

    Profile profileData() const;

private slots:
    void onTemplateChanged(int index);

private:
    void setupUi();
    void loadFromProfile(const Profile &profile);
    void rebuildAgentRowsForTemplate(const Template &t, const Profile &profile);
    const Template *currentTemplate() const;

    QList<Template> m_templates;
    Profile *m_initialProfile; // non-owning pointer used to cache initial data

    QString m_defaultModelId;

    QComboBox *m_templateCombo = nullptr;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_smallModelEdit = nullptr;
};
