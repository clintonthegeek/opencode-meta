#pragma once

#include <QDialog>

class ModelsBrowserWidget;
class QComboBox;
class QPlainTextEdit;
class QLabel;
class StorageManager;

// AddSpecialistDialog: pin a new Specialist to a Role and model.
//
// Layout (top to bottom):
//   1. Role selector     (QComboBox of available Roles)
//   2. Model picker      (ModelsBrowserWidget in pickerMode)
//   3. Optional override (QPlainTextEdit, raw string;
//                         {file: ...} objects can be entered verbatim)
//
// The dialog does not have its own OK/Cancel buttons. The embedder-side
// OK/Cancel inside ModelsBrowserWidget's pickerMode drive accept()/reject():
//   - modelAccepted(QString) -> accept()
//   - selectionCanceled()    -> reject()
//
// Required inputs are validated in accept(): at least one Role must exist
// and a model must be selected.
class AddSpecialistDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddSpecialistDialog(StorageManager &storageManager,
                                 QWidget *parent = nullptr);

    // F5: pre-select the Role whose id matches `roleId`. Must be called
    // BEFORE exec(). Empty roleId is a no-op. Used by the cross-view
    // smoke test harness so the inner Role picker does not have to be
    // driven manually.
    void setRoleId(const QString &roleId);

    // F5: pre-select the model whose id (e.g. "anthropic/claude-sonnet-4-6")
    // matches `modelId`. Must be called BEFORE exec(). Empty modelId is a
    // no-op. The selection is written onto the embedded ModelsBrowserWidget
    // so that selectedModelId() returns `modelId` once exec() runs. Used
    // by the cross-view smoke test harness.
    void setModelId(const QString &modelId);

    QString selectedRoleId() const;
    QString selectedModelId() const;
    QString promptOverrideText() const;

signals:
    // F2: emitted when the user accepts the dialog but no Role is available
    // so they cannot finish adding a Specialist. Hosts (MainWindow/route
    // chain) should switch to the Roles view and seed a new Role whose
    // name is seeded with `proposedName` (empty is acceptable -- the Role
    // editor surfaces a name field).
    void createRoleRequested(const QString &proposedName);

private slots:
    void onModelAccepted(const QString &modelId);
    void onPickerCanceled();

private:
    QComboBox *m_roleCombo = nullptr;
    ModelsBrowserWidget *m_modelsBrowser = nullptr;
    QPlainTextEdit *m_overrideEdit = nullptr;

    StorageManager &m_storageManager;
};
