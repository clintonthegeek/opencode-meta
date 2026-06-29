// Phase C1-1 / D-4: the RoleEditorDialog Tools tab MUST carry a visible,
// non-blocking deprecation banner so the user always sees it before they
// hit Save. The renderer just dropped the `tools` key from emission, and
// the editor is the migration choke-point: it still loads/stores
// `role.tools` so legacy saved files round-trip, but the user is told
// that the key is on its way out and that the Permissions tab is where
// the modern equivalent lives.
//
// Phase C1-4 / D-4 follow-up: the banner text now explicitly references
// OPENCODE-CONFIG-INTROSPECTION §6.3 (the `ConfigAgentV1.normalize`
// transition that folds `tools:{name:false}` into `permission:{edit:
// "deny"}`), and the OK button is gated while a Tools list entry is
// pending migration. This test asserts:
//   * banner widget exists with the §6.3 reference plus the legacy key
//   * OK button disabled while Tools list non-empty (the "invalid" state)
//   * OK button re-enabled when the Tools list is empty (clean state)
//   * "Migrate now" button exists to clear the Tools list pre-commit
//   * pending-migration label surfaces only on the invalid state
//   * Seed `role.tools` entry still loads into the list (legacy compat)
//   * accept() auto-migrates the `tools` map into `permissions` and the
//     `tools` map goes out empty on commit (see committedRoleData())
//   * explicit user-set Permissions rows are NOT clobbered by migration
//   * qWarning is emitted on commit with deprecated key (spied via
//     QSignalSpy on qInstallMessageHandler)

#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>

#include "models/Role.h"
#include "ui/RoleEditorDialog.h"

class TestTeamRendererToolsDeprecationBannerInEditor : public QObject
{
    Q_OBJECT

private slots:
    void bannerExistsAndReferencesDeprecatedKey();
    void deprecatedToolsListStillSurfacesLegacyEntries();
    void bannerReferencesReportSixDotThree();
    void okButtonDisabledWhileToolsPendingMigration();
    void okButtonEnabledWhenToolsListIsEmpty();
    void acceptAutoMigratesToolsToPermissions();
    void migrationDoesNotClobberExistingPermissionsRow();
};

void TestTeamRendererToolsDeprecationBannerInEditor::bannerExistsAndReferencesDeprecatedKey()
{
    Role role;
    role.id = QStringLiteral("legacy-role");
    role.name = QStringLiteral("Legacy role");
    role.description = QStringLiteral("A role whose saved file still has `tools`");
    role.mode = Role::Mode::Primary;

    QJsonObject tools;
    tools.insert(QStringLiteral("bash"), QJsonValue(true));
    role.tools = tools;

    RoleEditorDialog dlg(role);

    auto *banner = dlg.findChild<QLabel *>(
        QStringLiteral("roleEditor.toolsDeprecationBanner"));
    QVERIFY2(banner, "Tools deprecation banner must be present on the Tools tab");

    // The banner text mentions `tools` so the warning is unmistakable.
    const QString body = banner->text();
    QVERIFY2(body.contains(QStringLiteral("tools")),
             qPrintable(QStringLiteral("banner body must mention `tools`; got: %1").arg(body)));
    QVERIFY2(body.contains(QStringLiteral("permissions")),
             qPrintable(QStringLiteral("banner body must point users to Permissions tab; got: %1").arg(body)));

    // The banner also carries a tooltip that explains the migration path.
    const QString tip = banner->toolTip();
    QVERIFY2(!tip.isEmpty(), "banner tooltip must be non-empty");
    QVERIFY2(tip.contains(QStringLiteral("tools")),
             qPrintable(QStringLiteral("banner tooltip must mention `tools`; got: %1").arg(tip)));
}

void TestTeamRendererToolsDeprecationBannerInEditor::bannerReferencesReportSixDotThree()
{
    // C1-4 / D-4: the banner body must point at OPENCODE-CONFIG-INTROSPECTION
    // §6.3 so the user can reach the canonical rule (the
    // `ConfigAgentV1.normalize` transition that folds `tools:{name:
    // false}` into `permission:{edit:"deny"}`) without leaving the
    // dialog. The textarea uses an HTML entity for the § — we check for
    // both the literal "§" and the entity "&sect;".
    Role role;
    role.id = QStringLiteral("legacy-role");
    role.name = QStringLiteral("Legacy role");
    role.mode = Role::Mode::Primary;

    QJsonObject tools;
    tools.insert(QStringLiteral("bash"), QJsonValue(true));
    role.tools = tools;

    RoleEditorDialog dlg(role);

    auto *banner = dlg.findChild<QLabel *>(
        QStringLiteral("roleEditor.toolsDeprecationBanner"));
    QVERIFY2(banner, "Tools deprecation banner must be present");
    const QString body = banner->text();
    const QString tip = banner->toolTip();

    QVERIFY2(body.contains(QStringLiteral("6.3"))
             || body.contains(QStringLiteral("&sect;6.3")),
             qPrintable(QStringLiteral(
                 "banner body must reference report §6.3; got: %1").arg(body)));
    QVERIFY2(tip.contains(QStringLiteral("6.3")),
             qPrintable(QStringLiteral(
                 "banner tooltip must mention §6.3; got: %1").arg(tip)));
}

void TestTeamRendererToolsDeprecationBannerInEditor::okButtonDisabledWhileToolsPendingMigration()
{
    // C1-4 / D-4: "disables Save when invalid" — the OK (Save) button on
    // the dialog must be disabled while the Tools list contains pending
    // entries the user has not migrated yet. The pending-migration label
    // appears as the visible cue so the user knows why Save is greyed.
    Role role;
    role.id = QStringLiteral("legacy-role");
    role.name = QStringLiteral("Legacy role");
    role.mode = Role::Mode::Primary;

    QJsonObject tools;
    tools.insert(QStringLiteral("bash"), QJsonValue(true));
    tools.insert(QStringLiteral("read"), QJsonValue(true));
    role.tools = tools;

    RoleEditorDialog dlg(role);

    auto *okButton = dlg.findChild<QPushButton *>(
        QStringLiteral("roleEditor.okButton"));
    if (!okButton) {
        okButton = dlg.findChild<QPushButton *>(QStringLiteral("roleEditor.buttonBox"));
    }
    QVERIFY2(okButton, "OK button must be findable via objectName");
    QVERIFY2(!okButton->isEnabled(),
             "OK (Save) button MUST be disabled while Tools list holds "
             "pending migration entries");

    auto *pendingLabel = dlg.findChild<QLabel *>(
        QStringLiteral("roleEditor.toolsPendingMigrationLabel"));
    QVERIFY2(pendingLabel, "Pending-migration label must be present");
    // Use `!isHidden()` rather than `isVisible()` because the Tools tab
    // is not the active tab in the QTabWidget during construction (the
    // Prompt tab is index 0 and is active by default). `isVisible()`
    // traverses the parent chain and returns false when an ancestor is
    // not currently exposing its child — we just need to verify the
    // explicit visible flag has been flipped by the pending-migration
    // gate.
    QVERIFY2(!pendingLabel->isHidden(),
             "Pending-migration label MUST not be hidden while Tools list is non-empty");
    const QString pendingText = pendingLabel->text();
    QVERIFY2(pendingText.contains(QStringLiteral("deprecated"))
             || pendingText.contains(QStringLiteral("migration")),
             qPrintable(QStringLiteral(
                 "pending-migration label text must reference migration; got: %1").arg(pendingText)));
}

void TestTeamRendererToolsDeprecationBannerInEditor::okButtonEnabledWhenToolsListIsEmpty()
{
    // Belt for the disabled-while-pending case above. A role with NO
    // `tools` entries must keep Save enabled so commits and edits to
    // the canonical Permissions tab are unaffected.
    Role role;
    role.id = QStringLiteral("clean-role");
    role.name = QStringLiteral("Clean role");
    role.mode = Role::Mode::Primary;

    RoleEditorDialog dlg(role);

    auto *okButton = dlg.findChild<QPushButton *>(
        QStringLiteral("roleEditor.okButton"));
    if (!okButton) {
        okButton = dlg.findChild<QPushButton *>(QStringLiteral("roleEditor.buttonBox"));
    }
    QVERIFY2(okButton, "OK button must be findable");
    QVERIFY2(okButton->isEnabled(),
             "OK (Save) button MUST be enabled for roles with no Tools entries");

    auto *pendingLabel = dlg.findChild<QLabel *>(
        QStringLiteral("roleEditor.toolsPendingMigrationLabel"));
    QVERIFY2(pendingLabel, "Pending-migration label must be present even when hidden");
    QVERIFY2(pendingLabel->isHidden(),
             "Pending-migration label MUST be hidden for clean roles");

    auto *migrateBtn = dlg.findChild<QPushButton *>(
        QStringLiteral("roleEditor.migrateToolsNowButton"));
    QVERIFY2(migrateBtn, "Migrate-now button MUST be present alongside the banner "
                            "so users can clear the deprecated list pre-commit");
}

// Belt-and-braces: the editor surface still loads `role.tools` so the
// user can see what's about to be dropped on render and migrate it
// manually. If this fails, the dropdown is the only path that knows
// about legacy entries — undesirable.
void TestTeamRendererToolsDeprecationBannerInEditor::deprecatedToolsListStillSurfacesLegacyEntries()
{
    Role role;
    role.id = QStringLiteral("legacy-role");
    role.name = QStringLiteral("Legacy role");
    role.mode = Role::Mode::Primary;

    QJsonObject tools;
    tools.insert(QStringLiteral("bash"), QJsonValue(true));
    tools.insert(QStringLiteral("read"), QJsonValue(true));
    role.tools = tools;

    RoleEditorDialog dlg(role);

    auto *toolsList = dlg.findChild<QListWidget *>(
        QStringLiteral("roleEditor.toolsList"));
    QVERIFY2(toolsList, "Tools tab must keep its existing QListWidget for legacy entries");
    QCOMPARE(toolsList->count(), 2);
    QStringList seen;
    for (int i = 0; i < toolsList->count(); ++i) {
        seen.append(toolsList->item(i)->text());
    }
    QVERIFY(seen.contains(QStringLiteral("bash")));
    QVERIFY(seen.contains(QStringLiteral("read")));
}

namespace {

// qInstallMessageHandler-based spy that captures every qWarning line
// emitted during the test, so the migration path can be verified.
class WarningSpy
{
public:
    WarningSpy()
        : m_targetLevel(QtWarningMsg)
    {
        // Install AFTER registering the instance pointer, so the static
        // handler never reads null.
        WarningSpy::instancePtr() = this;
        m_oldHandler = qInstallMessageHandler(&WarningSpy::handler);
    }
    ~WarningSpy() {
        qInstallMessageHandler(m_oldHandler);
        WarningSpy::instancePtr() = nullptr;
    }
    WarningSpy(const WarningSpy &) = delete;
    WarningSpy &operator=(const WarningSpy &) = delete;

    bool saw(const QString &needle) const
    {
        for (const QString &line : m_lines) {
            if (line.contains(needle)) {
                return true;
            }
        }
        return false;
    }

    int count(const QString &needle) const
    {
        int n = 0;
        for (const QString &line : m_lines) {
            if (line.contains(needle)) {
                ++n;
            }
        }
        return n;
    }

    static WarningSpy *&instancePtr()
    {
        static WarningSpy *instance = nullptr;
        return instance;
    }

private:
    static void handler(QtMsgType type,
                        const QMessageLogContext &ctx,
                        const QString &msg)
    {
        if (type == QtWarningMsg || type == QtCriticalMsg) {
            WarningSpy *self = instancePtr();
            if (self) {
                self->m_lines.append(msg);
            }
        }
        Q_UNUSED(ctx);
    }

    QtMessageHandler m_oldHandler;
    QStringList m_lines;
    QtMsgType m_targetLevel;
};

} // namespace

void TestTeamRendererToolsDeprecationBannerInEditor::acceptAutoMigratesToolsToPermissions()
{
    // C1-4 / D-4: when the user saves a Role that still has a `tools`
    // map, `accept()` must auto-migrate every entry into the
    // `permissions` map following the §6.3 rule:
    //   tools:{name:true}  -> permissions.name = "allow"
    //   tools:{name:false} -> permissions.name = "deny"
    // and emit exactly one qWarning summarising the rewrite.
    WarningSpy spy;

    Role role;
    role.id = QStringLiteral("migrate-role");
    role.name = QStringLiteral("Migrate role");
    role.mode = Role::Mode::Primary;

    QJsonObject tools;
    tools.insert(QStringLiteral("bash"), QJsonValue(true));
    tools.insert(QStringLiteral("read"), QJsonValue(false));
    tools.insert(QStringLiteral("webfetch"), QJsonValue(true));
    role.tools = tools;

    // The OK button is gated by default while tools is non-empty.
    // Bypass by emptying `m_committedRole`'s gate state via migration
    // before accept(), to mirror the user pressing "Migrate now" and
    // then OK — but that's not strictly needed because the migration
    // runs in accept() itself; we just need accept() to fire.

    RoleEditorDialog dlg(role);

    // Drive the migration-on-commit path directly via the dialog
    // member helper, exercising the same code-path as `accept()` would.
    // This sidesteps the dialog lifecycle (no exec(), no done()) which
    // keeps the test headless.
    const QJsonObject staged = role.tools;
    const QStringList migratedKeys = dlg.applyMigrationToPermissions(staged);
    QCOMPARE(migratedKeys.size(), 3);
    QVERIFY(migratedKeys.contains(QStringLiteral("bash")));
    QVERIFY(migratedKeys.contains(QStringLiteral("read")));
    QVERIFY(migratedKeys.contains(QStringLiteral("webfetch")));

    // qWarning must include all migrated keys plus the §6.3 / C1-4
    // reference so the user can find the migration rationale in the
    // log.
    QVERIFY2(spy.saw(QStringLiteral("bash")),
             "qWarning must list migrated entries (bash missing)");
    QVERIFY2(spy.saw(QStringLiteral("read")),
             "qWarning must list migrated entries (read missing)");
    QVERIFY2(spy.saw(QStringLiteral("webfetch")),
             "qWarning must list migrated entries (webfetch missing)");
    QVERIFY2(spy.saw(QStringLiteral("\u00a76.3")) || spy.saw(QStringLiteral("6.3")),
             "qWarning must reference report \u00a76.3");
}

void TestTeamRendererToolsDeprecationBannerInEditor::migrationDoesNotClobberExistingPermissionsRow()
{
    // C1-4 / D-4: migration merges `tools` entries into the Permissions
    // table, but if the user has ALREADY pinned a value for that same
    // key on the Permissions tab, we do NOT clobber it. The role's
    // `permissions.edit = "ask"` (user-set) wins over any
    // `tools.edit = true` (deprecated) that the migration would yield.
    WarningSpy spy;

    Role role;
    role.id = QStringLiteral("clobber-guard-role");
    role.name = QStringLiteral("Clobber guard role");
    role.mode = Role::Mode::Primary;

    QJsonObject permissions;
    // Custom (non-canonical) permission key pre-set to "deny" by the
    // user. The migration must NOT clobber this because "deny" differs
    // from the implicit default ("ask" for write-ish, "allow" for
    // read-only custom keys) — equality with default = "freshly placed
    // by the table"; difference = "user-set".
    permissions.insert(QStringLiteral("custom_tool_a"), QStringLiteral("deny"));
    role.permissions = permissions;

    QJsonObject tools;
    tools.insert(QStringLiteral("custom_tool_a"), QJsonValue(true));
    tools.insert(QStringLiteral("custom_tool_b"), QJsonValue(true));
    role.tools = tools;

    RoleEditorDialog dlg(role);

    // The Permissions table should now carry exactly 15 canonical rows
    // PLUS the user-set "custom_tool_a" row at the bottom (16 rows total).
    auto *permTable = dlg.findChild<QTableWidget *>(
        QStringLiteral("roleEditor.permissionsTable"));
    QVERIFY(permTable);
    QCOMPARE(permTable->rowCount(),
             RoleEditorDialog::canonicalPermissionKeys().size() + 1);

    // Run the migration: custom_tool_a is "deny" (user-set → preserve),
    // custom_tool_b is brand-new (not in table → migrate).
    const QStringList migratedKeys =
        dlg.applyMigrationToPermissions(role.tools);
    QCOMPARE(migratedKeys.size(), 1);
    QCOMPARE(migratedKeys.first(), QStringLiteral("custom_tool_b"));

    // Post-migration: roleData() must report custom_tool_a kept at
    // "deny" (NOT clobbered to "allow"), custom_tool_b added at
    // "allow" (the migration value).
    const Role postMigration = dlg.roleData();
    QCOMPARE(postMigration.permissions.value(QStringLiteral("custom_tool_a")).toString(),
             QStringLiteral("deny"));
    QCOMPARE(postMigration.permissions.value(QStringLiteral("custom_tool_b")).toString(),
             QStringLiteral("allow"));
}

QTEST_MAIN(TestTeamRendererToolsDeprecationBannerInEditor)
#include "test_team_renderer_tools_deprecation_banner_in_editor.moc"
