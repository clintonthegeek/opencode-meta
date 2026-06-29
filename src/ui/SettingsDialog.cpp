#include "ui/SettingsDialog.h"

#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace {

constexpr const char *kGroup           = "settings";
constexpr const char *kKeyOpencodePath = "opencode_binary_path";
constexpr const char *kKeyStorageRoot  = "storage_root_path";
constexpr const char *kKeyTheme        = "theme";
// Phase D3-5 / D-9: storage-seed controls documented in
// `docs/plan/2026-06-29-stock-agent-fidelity.md`. Default true so
// a fresh install lands on the stock-aligned seed without user
// action.
constexpr const char *kKeySeedStockDefaults     = "seed_stock_defaults";
constexpr const char *kKeyResetSeedOnNextLaunch = "reset_seed_on_next_launch";

constexpr const char *kThemeSystem = "system";
constexpr const char *kThemeLight  = "light";
constexpr const char *kThemeDark   = "dark";

// Stable object names so tests can find widgets via findChild. Keep
// these in sync with the header comments.
constexpr const char *kObjOpencodePathEdit       = "settingsDialog.opencodePathEdit";
constexpr const char *kObjOpencodeBrowseButton  = "settingsDialog.opencodeBrowseButton";
constexpr const char *kObjStorageRootEdit        = "settingsDialog.storageRootEdit";
constexpr const char *kObjStorageRootBrowseButton= "settingsDialog.storageRootBrowseButton";
constexpr const char *kObjThemeCombo             = "settingsDialog.themeCombo";
// Phase D3-5 object names — kept as a separate group so test
// findChild calls don't accidentally collide with the existing
// settings widgets.
constexpr const char *kObjSeedStockDefaultsCheckBox = "settingsDialog.seedStockDefaultsCheckBox";
constexpr const char *kObjResetSeedCheckBox         = "settingsDialog.resetSeedCheckBox";
constexpr const char *kObjValidationLabel        = "settingsDialog.validationLabel";

SettingsDialog::Theme themeFromString(const QString &raw)
{
    if (raw == QLatin1String(kThemeLight)) {
        return SettingsDialog::Theme::Light;
    }
    if (raw == QLatin1String(kThemeDark)) {
        return SettingsDialog::Theme::Dark;
    }
    return SettingsDialog::Theme::System;
}

QString themeToString(SettingsDialog::Theme theme)
{
    switch (theme) {
    case SettingsDialog::Theme::Light:
        return QString::fromLatin1(kThemeLight);
    case SettingsDialog::Theme::Dark:
        return QString::fromLatin1(kThemeDark);
    case SettingsDialog::Theme::System:
    default:
        return QString::fromLatin1(kThemeSystem);
    }
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    resize(640, 320);

    buildUi();
    // Seed the dialog with app-level settings so the user sees the
    // current values immediately. If no settings have ever been
    // written, this is a no-op — everything stays at the dialog's
    // constructed defaults.
    populateFromValues(loadFromAppSettings());
    runValidation();
}

QString SettingsDialog::keyOpencodeBinaryPath()
{
    return QString::fromLatin1(kKeyOpencodePath);
}

QString SettingsDialog::keyStorageRootPath()
{
    return QString::fromLatin1(kKeyStorageRoot);
}

QString SettingsDialog::keyTheme()
{
    return QString::fromLatin1(kKeyTheme);
}

QString SettingsDialog::keySeedStockDefaults()
{
    return QString::fromLatin1(kKeySeedStockDefaults);
}

QString SettingsDialog::keyResetSeedOnNextLaunch()
{
    return QString::fromLatin1(kKeyResetSeedOnNextLaunch);
}

SettingsDialog::Values SettingsDialog::loadFromAppSettings()
{
    QSettings settings;
    return loadSettings(settings);
}

void SettingsDialog::saveToAppSettings(const Values &values)
{
    QSettings settings;
    writeSettings(values, settings);
}

SettingsDialog::Values SettingsDialog::loadSettings(QSettings &settings)
{
    Values out;
    settings.beginGroup(QString::fromLatin1(kGroup));
    out.opencodeBinaryPath = settings.value(
        QString::fromLatin1(kKeyOpencodePath)).toString();
    out.storageRootPath = settings.value(
        QString::fromLatin1(kKeyStorageRoot)).toString();
    out.theme = themeFromString(settings.value(
        QString::fromLatin1(kKeyTheme)).toString());
    // Phase D3-5: storage-seed controls. Default true so a fresh
    // install lands on the stock-aligned seed without user
    // intervention. The resetSeed flag is single-shot (the seeder
    // clears it after consuming).
    out.seedStockDefaults = settings.value(
        QString::fromLatin1(kKeySeedStockDefaults),
        QVariant(true)).toBool();
    out.resetSeedOnNextLaunch = settings.value(
        QString::fromLatin1(kKeyResetSeedOnNextLaunch),
        QVariant(false)).toBool();
    settings.endGroup();
    return out;
}

void SettingsDialog::writeSettings(const Values &values, QSettings &settings)
{
    settings.beginGroup(QString::fromLatin1(kGroup));
    settings.setValue(QString::fromLatin1(kKeyOpencodePath),
                      values.opencodeBinaryPath);
    settings.setValue(QString::fromLatin1(kKeyStorageRoot),
                      values.storageRootPath);
    settings.setValue(QString::fromLatin1(kKeyTheme),
                      themeToString(values.theme));
    settings.setValue(QString::fromLatin1(kKeySeedStockDefaults),
                      values.seedStockDefaults);
    settings.setValue(QString::fromLatin1(kKeyResetSeedOnNextLaunch),
                      values.resetSeedOnNextLaunch);
    settings.endGroup();
    settings.sync();
}

SettingsDialog::Values SettingsDialog::values() const
{
    Values out;
    out.opencodeBinaryPath = m_opencodePathEdit ? m_opencodePathEdit->text() : QString();
    out.storageRootPath    = m_storageRootEdit  ? m_storageRootEdit->text()  : QString();
    if (m_themeCombo) {
        out.theme = static_cast<Theme>(m_themeCombo->currentData().toInt());
    } else {
        out.theme = Theme::System;
    }
    out.seedStockDefaults = m_seedStockDefaultsCheckBox
        ? m_seedStockDefaultsCheckBox->isChecked()
        : true;
    out.resetSeedOnNextLaunch = m_resetSeedCheckBox
        ? m_resetSeedCheckBox->isChecked()
        : false;
    return out;
}

void SettingsDialog::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Configure global preferences. Theme is recorded but the "
           "live palette switch lands in a future update."),
        this);
    intro->setWordWrap(true);
    intro->setToolTip(tr(
        "Preferences apply app-wide. Stored values survive a restart; "
        "Edit -> Preferences... is the only entry point."));
    intro->setWhatsThis(tr(
        "<b>Preferences</b> &mdash; global defaults for the "
        "opencode-meta-qt app.<br/>"
        "<b>opencode binary path</b>: explicit path to the opencode "
        "executable; fall back to $PATH when blank.<br/>"
        "<b>Storage root</b>: directory holding roles, specialists, "
        "teams, trials, projects, and the models cache.<br/>"
        "<b>Theme</b>: persisted today, applied visually in Phase 4-2."));
    mainLayout->addWidget(intro);

    auto *formGroup = new QGroupBox(tr("Application"), this);
    auto *formLayout = new QFormLayout(formGroup);

    // ---- opencode binary path row ----
    auto *opencodeRow = new QWidget(formGroup);
    auto *opencodeRowLayout = new QHBoxLayout(opencodeRow);
    opencodeRowLayout->setContentsMargins(0, 0, 0, 0);

    m_opencodePathEdit = new QLineEdit(opencodeRow);
    m_opencodePathEdit->setObjectName(QString::fromLatin1(kObjOpencodePathEdit));
    m_opencodePathEdit->setPlaceholderText(tr("e.g. /usr/local/bin/opencode (leave empty to use $PATH)"));

    m_opencodeBrowseButton = new QPushButton(tr("Browse..."), opencodeRow);
    m_opencodeBrowseButton->setObjectName(QString::fromLatin1(kObjOpencodeBrowseButton));

    opencodeRowLayout->addWidget(m_opencodePathEdit, 1);
    opencodeRowLayout->addWidget(m_opencodeBrowseButton);

    auto *opencodeLabel = new QLabel(tr("&opencode binary:"), formGroup);
    opencodeLabel->setBuddy(m_opencodePathEdit);
    opencodeLabel->setToolTip(tr("Absolute path to the opencode executable. Leave blank to fall back to $PATH."));
    formLayout->addRow(opencodeLabel, opencodeRow);

    // ---- storage root path row ----
    auto *storageRow = new QWidget(formGroup);
    auto *storageRowLayout = new QHBoxLayout(storageRow);
    storageRowLayout->setContentsMargins(0, 0, 0, 0);

    m_storageRootEdit = new QLineEdit(storageRow);
    m_storageRootEdit->setObjectName(QString::fromLatin1(kObjStorageRootEdit));
    m_storageRootEdit->setPlaceholderText(
        tr("Optional override. Leave blank for ~/.opencode-meta. "
           "Holds roles/, teams/, trials/, projects/."));

    m_storageRootBrowseButton = new QPushButton(tr("Browse..."), storageRow);
    m_storageRootBrowseButton->setObjectName(QString::fromLatin1(kObjStorageRootBrowseButton));

    storageRowLayout->addWidget(m_storageRootEdit, 1);
    storageRowLayout->addWidget(m_storageRootBrowseButton);

    auto *storageLabel = new QLabel(tr("Storage &root:"), formGroup);
    storageLabel->setBuddy(m_storageRootEdit);
    storageLabel->setToolTip(
        tr("Optional override. Leave blank to fall back to "
           "~/.opencode-meta. If set, the directory must already exist."));
    formLayout->addRow(storageLabel, storageRow);

    // ---- theme row ----
    m_themeCombo = new QComboBox(formGroup);
    m_themeCombo->setObjectName(QString::fromLatin1(kObjThemeCombo));
    // Add placeholder rows first so setItemData/setItemText below
    // actually land on real entries (Qt silently no-ops on missing
    // indices which is exactly the bug that motivated this comment).
    m_themeCombo->addItem(QString(), static_cast<int>(Theme::System));
    m_themeCombo->addItem(QString(), static_cast<int>(Theme::Light));
    m_themeCombo->addItem(QString(), static_cast<int>(Theme::Dark));
    m_themeCombo->setItemText(0, tr("Follow System"));
    m_themeCombo->setItemText(1, tr("Light"));
    m_themeCombo->setItemText(2, tr("Dark"));

    auto *themeLabel = new QLabel(tr("&Theme:"), formGroup);
    themeLabel->setBuddy(m_themeCombo);
    themeLabel->setToolTip(tr("Stored preference only — the live palette switch arrives in a later update."));
    formLayout->addRow(themeLabel, m_themeCombo);

    mainLayout->addWidget(formGroup);

    // ---- Phase D3-5 / D-9 / D-12: Seeding section ----
    // Sits below the Application group per UI §4.2 ("seeding section
    // sits below the Storage root row" — we honor the intent even if
    // we restate it on a separate group for visual hierarchy). The
    // toggle defaults match the in-process defaults so a fresh
    // install picks the stock-aligned seed without user action.
    auto *seedGroup = new QGroupBox(tr("Seeding"), this);
    auto *seedLayout = new QVBoxLayout(seedGroup);

    m_seedStockDefaultsCheckBox = new QCheckBox(
        tr("Seed stock-aligned defaults on first run"), seedGroup);
    m_seedStockDefaultsCheckBox->setObjectName(
        QString::fromLatin1(kObjSeedStockDefaultsCheckBox));
    m_seedStockDefaultsCheckBox->setToolTip(tr(
        "When the storage root is empty, seed it with the seven stock "
        "opencode agents (Build, Plan, General, Explore + the three "
        "hidden primaries compaction/title/summary). Uncheck to seed "
        "the legacy opencode-meta approximation instead."));
    m_seedStockDefaultsCheckBox->setWhatsThis(tr(
        "When checked, an empty storage root is seeded with the seven "
        "<b>stock</b> opencode native agents, each carrying "
        "<code>metadata.native = true</code> so the editor can badge "
        "them and gate the Delete action. When unchecked the seeder "
        "falls back to the legacy opencode-meta fiction (a coarse "
        "build/plan/general approximation)."));
    seedLayout->addWidget(m_seedStockDefaultsCheckBox);

    m_resetSeedCheckBox = new QCheckBox(
        tr("Reset storage to stock defaults on next launch"), seedGroup);
    m_resetSeedCheckBox->setObjectName(
        QString::fromLatin1(kObjResetSeedCheckBox));
    m_resetSeedCheckBox->setToolTip(tr(
        "Wipes the existing roles and teams JSON and re-runs the "
        "stock-aligned seed. Use this if you want to compare your "
        "current settings with stock."));
    m_resetSeedCheckBox->setWhatsThis(tr(
        "<b>Single-shot</b> reset. The flag clears itself after the "
        "next launch so subsequent runs do not re-wipe. Existing "
        "user-created Roles and Teams outside the seed set are "
        "preserved (only the JSON files under roles/ and teams/ are "
        "removed, not the entire storage tree)."));
    seedLayout->addWidget(m_resetSeedCheckBox);

    mainLayout->addWidget(seedGroup);

    // ---- validation label ----
    m_validationLabel = new QLabel(this);
    m_validationLabel->setObjectName(QString::fromLatin1(kObjValidationLabel));
    m_validationLabel->setWordWrap(true);
    m_validationLabel->setTextFormat(Qt::RichText);
    QPalette palette = m_validationLabel->palette();
    palette.setColor(QPalette::WindowText, QColor(170, 30, 30));
    m_validationLabel->setPalette(palette);
    m_validationLabel->setVisible(false);
    mainLayout->addWidget(m_validationLabel);

    mainLayout->addStretch(1);

    // ---- standard OK / Cancel ----
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Save"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        // Persist before closing so Accept() always carries a committed
        // round-trip. Even if validation has flagged a problem, we
        // honor the user's intent — the validation label was made
        // visible while they were typing.
        saveToAppSettings(values());
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Wire browse buttons after the rest of the widget tree exists so
    // QFileDialog parents have a stable window handle.
    connect(m_opencodeBrowseButton, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseOpencodeBinary);
    connect(m_storageRootBrowseButton, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseStorageRoot);

    // Re-validate as the user types / changes selection so warning
    // text tracks UI state. Wired to whichever editor signals fire
    // on user input.
    if (m_opencodePathEdit) {
        connect(m_opencodePathEdit, &QLineEdit::textChanged,
                this, &SettingsDialog::onFieldChanged);
    }
    if (m_storageRootEdit) {
        connect(m_storageRootEdit, &QLineEdit::textChanged,
                this, &SettingsDialog::onFieldChanged);
    }
    connect(m_themeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onFieldChanged);
}

void SettingsDialog::populateFromValues(const Values &in)
{
    if (m_opencodePathEdit) {
        m_opencodePathEdit->setText(in.opencodeBinaryPath);
    }
    if (m_storageRootEdit) {
        m_storageRootEdit->setText(in.storageRootPath);
    }
    if (m_themeCombo) {
        const int target = static_cast<int>(in.theme);
        const int idx = m_themeCombo->findData(target);
        if (idx >= 0) {
            m_themeCombo->setCurrentIndex(idx);
        }
    }
    if (m_seedStockDefaultsCheckBox) {
        m_seedStockDefaultsCheckBox->setChecked(in.seedStockDefaults);
    }
    if (m_resetSeedCheckBox) {
        m_resetSeedCheckBox->setChecked(in.resetSeedOnNextLaunch);
    }
}

void SettingsDialog::onBrowseOpencodeBinary()
{
    if (!m_opencodePathEdit) {
        return;
    }

    const QString start = m_opencodePathEdit->text();
    const QString dir = start.isEmpty()
                          ? QDir::homePath()
                          : QFileInfo(start).absolutePath();
    const QString chosen = QFileDialog::getOpenFileName(
        this,
        tr("Choose opencode binary"),
        dir,
        tr("All files (*)"));
    if (chosen.isEmpty()) {
        return;
    }
    m_opencodePathEdit->setText(chosen);
    // textChanged triggers onFieldChanged -> runValidation.
}

void SettingsDialog::onBrowseStorageRoot()
{
    if (!m_storageRootEdit) {
        return;
    }

    const QString start = m_storageRootEdit->text();
    const QString dir = start.isEmpty()
                          ? QDir::homePath()
                          : start;
    const QString chosen = QFileDialog::getExistingDirectory(
        this,
        tr("Choose storage root directory"),
        dir);
    if (chosen.isEmpty()) {
        return;
    }
    m_storageRootEdit->setText(chosen);
}

void SettingsDialog::onFieldChanged()
{
    runValidation();
}

void SettingsDialog::runValidation()
{
    if (!m_validationLabel) {
        return;
    }

    QStringList warnings;

    // opencode binary: empty allowed (means "$PATH fallback"). Anywhere
    // else, the path must point at an existing regular file.
    const QString opencodePath = m_opencodePathEdit ? m_opencodePathEdit->text().trimmed() : QString();
    if (!opencodePath.isEmpty()) {
        const QFileInfo info(opencodePath);
        if (!info.exists() || !info.isFile()) {
            warnings << tr("opencode binary path does not point at an existing file: <code>%1</code>")
                            .arg(opencodePath.toHtmlEscaped());
        }
    }

    // Storage root: optional override. Empty is fine (we fall back to
    // ~/.opencode-meta on the read side). A non-empty value that does
    // not resolve to an existing directory is still a warning so the
    // user knows their override will not take effect.
    const QString storagePath = m_storageRootEdit ? m_storageRootEdit->text().trimmed() : QString();
    if (!storagePath.isEmpty()) {
        const QFileInfo info(storagePath);
        if (!info.exists()) {
            warnings << tr("Storage root override does not exist "
                           "(will fall back to default): <code>%1</code>")
                            .arg(storagePath.toHtmlEscaped());
        } else if (!info.isDir()) {
            warnings << tr("Storage root exists but is not a directory: <code>%1</code>")
                            .arg(storagePath.toHtmlEscaped());
        }
    }

    if (warnings.isEmpty()) {
        m_validationLabel->clear();
        m_validationLabel->setVisible(false);
    } else {
        m_validationLabel->setText(warnings.join(QStringLiteral("<br/>")));
        m_validationLabel->setVisible(true);
    }
}
