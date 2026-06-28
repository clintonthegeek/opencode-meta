#pragma once

#include <QDialog>

class QComboBox;
class QTextEdit;
class StorageManager;

// Lightweight dialog to compare two saved Profiles by rendering their
// opencode.json configs side by side and summarizing top-level differences.
class ProfileCompareDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProfileCompareDialog(StorageManager &storageManager, QWidget *parent = nullptr);

private slots:
    void onSelectionChanged();

private:
    void setupUi();
    void populateProfiles();
    void updateViews();

    StorageManager &m_storageManager;
    QComboBox *m_leftCombo = nullptr;
    QComboBox *m_rightCombo = nullptr;
    QTextEdit *m_leftText = nullptr;
    QTextEdit *m_rightText = nullptr;
    QTextEdit *m_diffSummary = nullptr;
};
