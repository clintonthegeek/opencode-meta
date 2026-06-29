// Round-trip + UI smoke test for SettingsDialog (ROADMAP P2-3).
//
// What this test locks down:
//   1. SettingsDialog constructs cleanly (no exceptions, all widgets
//      reachable through their stable objectNames).
//   2. Defaults are sensible: Theme = System, empty paths.
//   3. UI edits propagate to values() for every field.
//   4. Validation surfaces a warning for a known-bad binary path and
//      does NOT surface one for an empty path ("$PATH fallback" is
//      allowed).
//   5. Round-trip via QSettings holds the data lossless. We use the
//      public loadSettings()/writeSettings() helpers with a QSettings
//      that points at a temp INI, so no real user prefs are touched.
//   6. Accept persists the current values() through saveToAppSettings:
//      the test sets a unique organization+application name so the
//      write goes nowhere user-visible, and then verifies via the
//      loadBack() helper that the values survived.
//
// Dialog widgets are NEVER shown; the harness edits values by
// reaching into the QObject tree via objectName. That keeps the test
// deterministic and avoids needing a running event loop.

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTest>

#include "ui/SettingsDialog.h"

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void constructsWithAllWidgets();
    void defaultsAreSensible();
    void editsPropagateToValues();
    void emptyOpencodePathIsValid();
    void nonexistentOpencodePathWarns();
    void missingStorageRootWarns();
    void roundTripsThroughQSettings();
    void acceptPersistsViaAppSettings();

private:
    static QString lineTextFrom(const QObject *parent, const QString &name);
    static int     comboIndexFrom(const QObject *parent, const QString &name);

    QTemporaryDir m_tmpRoot;
    QTemporaryDir m_appCfgRoot;
};

void TestSettingsDialog::initTestCase()
{
    QVERIFY(m_tmpRoot.isValid());
    QVERIFY(m_appCfgRoot.isValid());

    // Force QSettings into an isolated, file-backed format so the
    // acceptPersistsViaAppSettings case does not leak into anywhere
    // user-visible (e.g. ~/.config). This matches the pattern used
    // elsewhere in tests/ — see test_confirm_apply_dialog.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_appCfgRoot.path());

    QCoreApplication::setOrganizationName(QStringLiteral("opencode-meta-tests"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("opencode-meta-tests.local"));
    QCoreApplication::setApplicationName(QStringLiteral("settings-dialog-test"));

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
}

void TestSettingsDialog::cleanupTestCase()
{
    // Reset org/app so subsequent tests in the same process do not
    // inherit our namespace. QSettings default format is process-wide
    // state, so we leave the IniFormat+UserScope settings in place
    // for any later test that might want them.
}

QString TestSettingsDialog::lineTextFrom(const QObject *parent, const QString &name)
{
    const QLineEdit *le = parent->findChild<const QLineEdit *>(name);
    return le ? le->text() : QString();
}

int TestSettingsDialog::comboIndexFrom(const QObject *parent, const QString &name)
{
    const QComboBox *combo = parent->findChild<const QComboBox *>(name);
    return combo ? combo->currentIndex() : -1;
}

void TestSettingsDialog::constructsWithAllWidgets()
{
    SettingsDialog dlg;
    QVERIFY(dlg.findChild<QLineEdit *>(QStringLiteral("settingsDialog.opencodePathEdit")));
    QVERIFY(dlg.findChild<QLineEdit *>(QStringLiteral("settingsDialog.storageRootEdit")));
    QVERIFY(dlg.findChild<QComboBox *>(QStringLiteral("settingsDialog.themeCombo")));
    QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("settingsDialog.opencodeBrowseButton")));
    QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("settingsDialog.storageRootBrowseButton")));
    QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("settingsDialog.validationLabel")));
    QVERIFY(dlg.findChild<QDialogButtonBox *>());
}

void TestSettingsDialog::defaultsAreSensible()
{
    SettingsDialog dlg;

    QCOMPARE(lineTextFrom(&dlg, QStringLiteral("settingsDialog.opencodePathEdit")), QString());
    QCOMPARE(lineTextFrom(&dlg, QStringLiteral("settingsDialog.storageRootEdit")), QString());

    auto *theme = dlg.findChild<QComboBox *>(QStringLiteral("settingsDialog.themeCombo"));
    QVERIFY(theme);
    QCOMPARE(theme->currentData().toInt(),
             static_cast<int>(SettingsDialog::Theme::System));

    const SettingsDialog::Values v = dlg.values();
    QCOMPARE(v.theme, SettingsDialog::Theme::System);
    QVERIFY(v.opencodeBinaryPath.isEmpty());
    QVERIFY(v.storageRootPath.isEmpty());
}

void TestSettingsDialog::editsPropagateToValues()
{
    SettingsDialog dlg;

    QLineEdit *opencodeEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.opencodePathEdit"));
    QVERIFY(opencodeEdit);
    opencodeEdit->setText(QStringLiteral("/usr/local/bin/opencode"));

    QLineEdit *storageEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.storageRootEdit"));
    QVERIFY(storageEdit);
    storageEdit->setText(m_tmpRoot.path());

    QComboBox *theme = dlg.findChild<QComboBox *>(
        QStringLiteral("settingsDialog.themeCombo"));
    QVERIFY(theme);
    const int darkIdx = theme->findData(static_cast<int>(SettingsDialog::Theme::Dark));
    QVERIFY(darkIdx >= 0);
    theme->setCurrentIndex(darkIdx);

    const SettingsDialog::Values v = dlg.values();
    QCOMPARE(v.opencodeBinaryPath, QStringLiteral("/usr/local/bin/opencode"));
    QCOMPARE(v.storageRootPath, m_tmpRoot.path());
    QCOMPARE(v.theme, SettingsDialog::Theme::Dark);
}

void TestSettingsDialog::emptyOpencodePathIsValid()
{
    SettingsDialog dlg;

    // Empty binary path -> "$PATH fallback" is allowed -> no warning.
    QLineEdit *opencodeEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.opencodePathEdit"));
    QVERIFY(opencodeEdit);
    opencodeEdit->setText(QString());

    // Storage root must be set, so we have to wire a valid dir to
    // avoid spurious warnings from the storage-root validator.
    QLineEdit *storageEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.storageRootEdit"));
    QVERIFY(storageEdit);
    storageEdit->setText(m_tmpRoot.path());

    QLabel *validation = dlg.findChild<QLabel *>(
        QStringLiteral("settingsDialog.validationLabel"));
    QVERIFY(validation);
    // As in the negative cases: the dialog isn't shown, so we read
    // the label text rather than isVisible. An empty text means either
    // "no warnings were raised" or "warnings were cleared".
    QVERIFY2(validation->text().isEmpty(),
             qPrintable(QStringLiteral("unexpected validation: ") + validation->text()));
}

void TestSettingsDialog::nonexistentOpencodePathWarns()
{
    SettingsDialog dlg;

    QLineEdit *opencodeEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.opencodePathEdit"));
    QVERIFY(opencodeEdit);
    opencodeEdit->setText(m_tmpRoot.path() + QStringLiteral("/does-not-exist-binary"));

    QLineEdit *storageEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.storageRootEdit"));
    QVERIFY(storageEdit);
    storageEdit->setText(m_tmpRoot.path());

    QLabel *validation = dlg.findChild<QLabel *>(
        QStringLiteral("settingsDialog.validationLabel"));
    QVERIFY(validation);
    // Check the label TEXT rather than isVisible(): the dialog is
    // never shown in the test harness, so ancestor visible-chains
    // hide everything. The text is the signal we actually care about
    // (set by runValidation()).
    QVERIFY2(!validation->text().isEmpty(),
             "validation label should carry a warning for a missing opencode binary");
    QVERIFY2(validation->text().contains(QStringLiteral("does not point")),
             qPrintable(QStringLiteral("expected opencode warning, got: ") + validation->text()));
}

void TestSettingsDialog::missingStorageRootWarns()
{
    SettingsDialog dlg;

    QLineEdit *storageEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.storageRootEdit"));
    QVERIFY(storageEdit);
    storageEdit->setText(QString());

    QLabel *validation = dlg.findChild<QLabel *>(
        QStringLiteral("settingsDialog.validationLabel"));
    QVERIFY(validation);
    QVERIFY2(!validation->text().isEmpty(),
             "validation label should carry a warning for a missing storage root");
    QVERIFY2(validation->text().contains(QStringLiteral("Storage root is required")),
             qPrintable(QStringLiteral("expected storage-required warning, got: ") + validation->text()));
}

void TestSettingsDialog::roundTripsThroughQSettings()
{
    // Write to a temp INI through the static writeSettings(), read
    // back through loadSettings(). Both APIs must use the same keys,
    // so this exercises the contract end-to-end without any dialog.
    QTemporaryFile ini;
    QVERIFY(ini.open());
    ini.close();

    QSettings writer(ini.fileName(), QSettings::IniFormat);
    SettingsDialog::Values in;
    in.opencodeBinaryPath = QStringLiteral("/opt/bin/opencode");
    in.storageRootPath    = m_tmpRoot.path();
    in.theme              = SettingsDialog::Theme::Light;
    SettingsDialog::writeSettings(in, writer);
    writer.sync();

    QSettings reader(ini.fileName(), QSettings::IniFormat);
    const SettingsDialog::Values out = SettingsDialog::loadSettings(reader);

    QCOMPARE(out.opencodeBinaryPath, in.opencodeBinaryPath);
    QCOMPARE(out.storageRootPath, in.storageRootPath);
    QCOMPARE(out.theme, in.theme);

    // Also verify the keys live under the documented group.
    QCOMPARE(SettingsDialog::keyOpencodeBinaryPath(),
             QStringLiteral("opencode_binary_path"));
    QCOMPARE(SettingsDialog::keyStorageRootPath(),
             QStringLiteral("storage_root_path"));
    QCOMPARE(SettingsDialog::keyTheme(),
             QStringLiteral("theme"));
}

void TestSettingsDialog::acceptPersistsViaAppSettings()
{
    // Drive the dialog UI, then call saveToAppSettings(values())
    // explicitly (mirroring what Accept does internally). Use a
    // freshly cleared app-level QSettings so we can assert a clean
    // round-trip.
    {
        QSettings cleaner;
        cleaner.clear();
        cleaner.sync();
    }

    SettingsDialog dlg;

    QLineEdit *opencodeEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.opencodePathEdit"));
    QVERIFY(opencodeEdit);
    opencodeEdit->setText(QStringLiteral("/tmp/some/opencode-binary"));

    QLineEdit *storageEdit = dlg.findChild<QLineEdit *>(
        QStringLiteral("settingsDialog.storageRootEdit"));
    QVERIFY(storageEdit);
    storageEdit->setText(m_tmpRoot.path());

    QComboBox *theme = dlg.findChild<QComboBox *>(
        QStringLiteral("settingsDialog.themeCombo"));
    QVERIFY(theme);
    theme->setCurrentIndex(theme->findData(static_cast<int>(SettingsDialog::Theme::Dark)));

    SettingsDialog::saveToAppSettings(dlg.values());

    QSettings reader;
    reader.beginGroup(QStringLiteral("settings"));
    QCOMPARE(reader.value(QStringLiteral("opencode_binary_path")).toString(),
             QStringLiteral("/tmp/some/opencode-binary"));
    QCOMPARE(reader.value(QStringLiteral("storage_root_path")).toString(),
             m_tmpRoot.path());
    QCOMPARE(reader.value(QStringLiteral("theme")).toString(),
             QStringLiteral("dark"));
    reader.endGroup();

    // The static loadFromAppSettings() must reproduce the same Values.
    const SettingsDialog::Values back = SettingsDialog::loadFromAppSettings();
    QCOMPARE(back.opencodeBinaryPath, QStringLiteral("/tmp/some/opencode-binary"));
    QCOMPARE(back.storageRootPath, m_tmpRoot.path());
    QCOMPARE(back.theme, SettingsDialog::Theme::Dark);
}

QTEST_MAIN(TestSettingsDialog)
#include "test_settings_dialog.moc"
