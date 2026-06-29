// SettingsDialog.h
//
// ROADMAP P2-3 – first-class preferences dialog.
//
// Models the editable surface for global app preferences. Versioned
// round-trip is intentionally trivial: each field maps one-to-one to a
// QSettings key under the "settings" group, so a future release can
// safely add keys without breaking old dialog sessions.
//
// Settings keys (QSettings "settings/" group):
//   settings/opencode_binary_path   QString – absolute path
//   settings/storage_root_path      QString – absolute path
//   settings/theme                  QString – "system" | "light" | "dark"
//
// Theme is stored today but the actual palette/stylesheet switch is
// intentionally NOT wired up (ROADMAP P4-2). The SettingsDialog is the
// persistence side; the live-switch side lives elsewhere.
//
// Validation:
//   * opencodeBinaryPath: empty allowed (means "use $PATH"). Non-empty
//     values must point at an existing file.
//   * storageRootPath:    optional override. Empty means "use default
//     (~/.opencode-meta)". Non-empty values that do not exist as a
//     directory are still surfaced as warnings so the user knows the
//     override will not take effect.
//   Validation feedback shows in a red label below the form but does
//   NOT disable OK — the user can still accept once the warning is
//   acknowledged via visual inspection (the warning stays after Accept
//   so the user is never surprised).
//
// Accessibility: every row has a buddy label so screen readers can
// announce control names consistently. objectNames are stable on the
// widgets that tests reach for.

#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Theme {
        System = 0,
        Light  = 1,
        Dark   = 2,
    };

    // Snapshot of every field exposed by the dialog. Tests use this
    // to assert that the UI round-trips cleanly. Strings are stored
    // verbatim — no path canonicalization here, that's the OS's job.
    struct Values {
        QString opencodeBinaryPath;
        QString storageRootPath;
        Theme   theme = Theme::System;
    };

    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override = default;

    // Snapshot the current UI state. Always reflects what the user
    // sees (regardless of whether Accept was clicked).
    Values values() const;

    // Round-trip helpers. Both are static so tests can use them
    // without instantiating the whole dialog. Both take a non-const
    // QSettings because QSettings::beginGroup()/endGroup() mutate the
    // internal group stack — the caller always owns a writable
    // QSettings (config / QSettings::IniFormat with a temp file in
    // tests).
    static void   writeSettings(const Values &values, QSettings &settings);
    static Values loadSettings(QSettings &settings);

    static QString keyOpencodeBinaryPath();
    static QString keyStorageRootPath();
    static QString keyTheme();

    // Convenience accessors that wrap the QSettings round-trip using
    // the application's default QSettings (organization + app name as
    // set on QCoreApplication). Calling code in MainWindow uses these
    // on Accept and on startup so persisted values take effect.
    static Values loadFromAppSettings();
    static void   saveToAppSettings(const Values &values);

private slots:
    void onBrowseOpencodeBinary();
    void onBrowseStorageRoot();
    void onFieldChanged();

private:
    void buildUi();
    void populateFromValues(const Values &values);
    void runValidation();

    QLineEdit    *m_opencodePathEdit      = nullptr;
    QPushButton  *m_opencodeBrowseButton  = nullptr;
    QLineEdit    *m_storageRootEdit       = nullptr;
    QPushButton  *m_storageRootBrowseButton = nullptr;
    QComboBox    *m_themeCombo            = nullptr;
    QLabel       *m_validationLabel       = nullptr;

    QPushButton  *m_okButton              = nullptr;
};
