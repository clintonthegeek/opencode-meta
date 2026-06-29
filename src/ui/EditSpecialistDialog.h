// EditSpecialistDialog: in-place editor for a Team's existing Specialist.
//
// ROADMAP P1-4: lets the user change a Specialist's name, model, and prompt
// override WITHOUT removing and re-adding the binding. The Role is fixed
// (changing it would violate the PARADIGM §2.3 "distinct Roles per Team"
// invariant); the binding's specialistId is also fixed. Everything else is
// editable.
//
// Layout (top to bottom):
//   1. Specialist id (read-only reference label).
//   2. Name field      (QLineEdit, optional human label; empty is allowed).
//   3. Model picker    (ModelsBrowserWidget in pickerMode; pre-selected to
//                      the Specialist's current modelId so a no-op edit
//                      just re-confirms the same model).
//   4. Optional prompt override (QPlainTextEdit, plain text or
//                      {file: ...} JSON verbatim).
//   5. Effective Prompt preview (PromptPreview) so the user sees the
//                      merged prompt + token count before accepting.
//
// The dialog does not own its own OK/Cancel buttons. The embedder-side
// OK/Cancel inside ModelsBrowserWidget's pickerMode drive accept()/reject():
//   - modelAccepted(QString) -> accept()
//   - selectionCanceled()    -> reject()
//
// Tested via the public setters selectedName()/selectedModelId()/
// promptOverrideText() and the convenience editedSpecialist() which merges
// the field values back into a Specialist record that preserves the
// immutable id+roleId fields.
#pragma once

#include <QDialog>

#include "models/Specialist.h"

class QLineEdit;
class QPlainTextEdit;
class QLabel;
class ModelsBrowserWidget;
class PromptPreview;
class StorageManager;

class EditSpecialistDialog : public QDialog
{
    Q_OBJECT

public:
    // Build an editor pre-filled with `initial.id`, `initial.roleId`,
    // `initial.name`, `initial.modelId`, and `initial.promptOverride`.
    // The immutable binding fields (id, roleId) survive editedSpecialist().
    explicit EditSpecialistDialog(const Specialist &initial,
                                 StorageManager &storageManager,
                                 QWidget *parent = nullptr);

    // F5-style test seam: pre-select the model row whose id matches
    // `modelId` in the embedded ModelsBrowserWidget BEFORE exec(). Empty
    // modelId is a no-op. Mirrors AddSpecialistDialog::setModelId() so
    // the cross-view smoke harness can drive rows without manual clicks.
    void setModelId(const QString &modelId);

    QString selectedName() const;
    QString selectedModelId() const;
    QString promptOverrideText() const;

    // Returns a Specialist representing the user's edits, preserving the
    // immutable id+roleId from the constructor input and copying through
    // any unchanged metadata field. Empty override text collapses the
    // promptOverride back to QJsonValue(QJsonValue::Undefined) so the
    // JSON writer drops the field (matches AddSpecialistDialog's trim
    // behavior).
    Specialist editedSpecialist() const;

private slots:
    void onModelAccepted(const QString &modelId);
    void onPickerCanceled();
    void refreshPromptPreview();

private:
    Specialist m_initial;
    StorageManager &m_storageManager;

    QLabel *m_idLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_overrideEdit = nullptr;
    ModelsBrowserWidget *m_modelsBrowser = nullptr;
    PromptPreview *m_promptPreview = nullptr;
};
