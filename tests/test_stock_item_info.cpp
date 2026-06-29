// Verifies stock-row affordances: the badge tooltip and the tiny info action.

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTimer>

#include "models/Role.h"
#include "models/Team.h"
#include "storage/StorageManager.h"
#include "ui/RolesWidget.h"
#include "ui/TeamsDialog.h"
#include "ui/TeamsWidget.h"

namespace {

constexpr const char *kStockTooltip =
    "Stock seed item - cannot be modified or deleted. Created automatically for new users.";

int findStockRow(QTableWidget *table)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *item = table->item(row, 0);
        if (item && item->data(Qt::UserRole + 1).toBool()) {
            return row;
        }
    }
    return -1;
}

int findTeamRow(QTableWidget *table, const QString &teamId)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *item = table->item(row, 0);
        if (!item) {
            continue;
        }
        const QString id = item->data(Qt::UserRole).toString().isEmpty()
            ? item->text()
            : item->data(Qt::UserRole).toString();
        if (id == teamId) {
            return row;
        }
    }
    return -1;
}

void showStockRows(QCheckBox *showStock, QWidget *widget)
{
    QVERIFY(showStock);
    showStock->setChecked(true);
    widget->show();
    QApplication::processEvents();
}

void verifyInfoAction(QAction *action, QWidget *widget, const QString &expectedKind)
{
    QVERIFY(action);
    QCOMPARE(action->text(), QStringLiteral("About this stock item"));

    QString capturedTitle;
    QString capturedText;
    bool captured = false;

    QTimer::singleShot(0, widget, [&]() {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(box);
        capturedTitle = box->windowTitle();
        capturedText = box->text();
        captured = true;
        QMetaObject::invokeMethod(box, "accept", Qt::QueuedConnection);
    });

    action->trigger();

    QVERIFY(captured);
    QCOMPARE(capturedTitle, QStringLiteral("About this stock item"));
    QVERIFY(capturedText.contains(QStringLiteral("stock seed data")));
    QVERIFY(capturedText.contains(QStringLiteral("settings/seed_stock_defaults")));
    QVERIFY(capturedText.contains(expectedKind));
}

template <typename WidgetT>
void verifyHiddenStockBanner(WidgetT &widget,
                              const QString &showStockBoxName,
                              const QString &bannerName,
                              const QString &buttonName)
{
    auto *showStock = widget.template findChild<QCheckBox *>(showStockBoxName);
    QVERIFY(showStock);
    QVERIFY(!showStock->isChecked());

    auto *banner = widget.template findChild<QLabel *>(bannerName);
    QVERIFY(banner);
    QVERIFY(banner->isVisible());
    QVERIFY(banner->text().contains(QStringLiteral("stock items are hidden")));

    auto *button = widget.template findChild<QPushButton *>(buttonName);
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Show them"));

    button->click();
    QTRY_VERIFY(showStock->isChecked());
    QTRY_VERIFY(!banner->isVisible());
}

template <typename WidgetT>
void verifyStockBadgeAndAction(WidgetT &widget,
                               const QString &showStockBoxName,
                               const QString &actionName,
                               const QString &kind)
{
    auto *showStock = widget.template findChild<QCheckBox *>(showStockBoxName);
    showStockRows(showStock, &widget);

    auto *table = widget.template findChild<QTableWidget *>();
    QVERIFY(table);

    const int stockRow = findStockRow(table);
    QVERIFY(stockRow >= 0);
    table->setCurrentCell(stockRow, 0);
    QApplication::processEvents();

    auto *nameCell = table->cellWidget(stockRow, 1);
    QVERIFY(nameCell);

    auto *badge = nameCell->template findChild<QLabel *>(QStringLiteral("stockBadge"));
    QVERIFY(badge);
    QCOMPARE(badge->toolTip(), QString::fromLatin1(kStockTooltip));

    auto *button = nameCell->template findChild<QToolButton *>(QStringLiteral("stockInfoButton"));
    QVERIFY(button);

    auto *action = widget.template findChild<QAction *>(actionName);
    verifyInfoAction(action, &widget, kind);
}

} // namespace

class TestStockItemInfo : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rolesStockBadgeAndInfoAction();
    void teamsStockBadgeAndInfoAction();
    void rolesHiddenStockBanner();
    void teamsHiddenStockBanner();
    void applyDialogShowsStockAndDefaultAgentHints();
};

void TestStockItemInfo::initTestCase()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
}

void TestStockItemInfo::rolesStockBadgeAndInfoAction()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    StorageManager storage(tmpRoot.path());

    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.metadata.insert(QStringLiteral("native"), true);
    QVERIFY(storage.saveRole(role));

    RolesWidget widget(storage);
    verifyStockBadgeAndAction(widget,
                              QStringLiteral("rolesWidget.showStock"),
                              QStringLiteral("rolesWidget.aboutStockItemAction"),
                              QStringLiteral("Role"));
}

void TestStockItemInfo::teamsStockBadgeAndInfoAction()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    StorageManager storage(tmpRoot.path());

    Team team;
    team.id = QStringLiteral("starter-team");
    team.name = QStringLiteral("Starter Team");
    team.metadata.insert(QStringLiteral("default_agent"), QStringLiteral("starter-build"));
    QVERIFY(storage.saveTeam(team));

    TeamsWidget widget(storage);
    verifyStockBadgeAndAction(widget,
                              QStringLiteral("teamsWidget.showStock"),
                              QStringLiteral("teamsWidget.aboutStockItemAction"),
                              QStringLiteral("Team"));
}

void TestStockItemInfo::rolesHiddenStockBanner()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    QSettings().setValue(QStringLiteral("settings/roles_show_stock"), false);

    StorageManager storage(tmpRoot.path());

    Role role;
    role.id = QStringLiteral("build");
    role.name = QStringLiteral("Build");
    role.metadata.insert(QStringLiteral("native"), true);
    QVERIFY(storage.saveRole(role));

    RolesWidget widget(storage);
    widget.show();
    QApplication::processEvents();

    verifyHiddenStockBanner(widget,
                            QStringLiteral("rolesWidget.showStock"),
                            QStringLiteral("rolesWidget.stockHiddenBannerLabel"),
                            QStringLiteral("rolesWidget.showHiddenStockButton"));
}

void TestStockItemInfo::teamsHiddenStockBanner()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    QSettings().setValue(QStringLiteral("settings/teams_show_stock"), false);

    StorageManager storage(tmpRoot.path());

    Team team;
    team.id = QStringLiteral("starter-team");
    team.name = QStringLiteral("Starter Team");
    team.metadata.insert(QStringLiteral("default_agent"), QStringLiteral("starter-build"));
    QVERIFY(storage.saveTeam(team));

    TeamsWidget widget(storage);
    widget.show();
    QApplication::processEvents();

    verifyHiddenStockBanner(widget,
                            QStringLiteral("teamsWidget.showStock"),
                            QStringLiteral("teamsWidget.stockHiddenBannerLabel"),
                            QStringLiteral("teamsWidget.showHiddenStockButton"));
}

void TestStockItemInfo::applyDialogShowsStockAndDefaultAgentHints()
{
    QTemporaryDir tmpRoot;
    QVERIFY(tmpRoot.isValid());

    StorageManager storage(tmpRoot.path());

    Team stockTeam;
    stockTeam.id = QStringLiteral("starter-team");
    stockTeam.name = QStringLiteral("Starter Team");
    stockTeam.metadata.insert(QStringLiteral("default_agent"), QStringLiteral("starter-build"));
    QVERIFY(storage.saveTeam(stockTeam));

    Team customTeam;
    customTeam.id = QStringLiteral("custom-team");
    customTeam.name = QStringLiteral("Custom Team");
    customTeam.metadata.insert(QStringLiteral("default_agent"), QStringLiteral("custom-build"));
    QVERIFY(storage.saveTeam(customTeam));

    TeamsDialog dialog(storage);
    dialog.show();
    QApplication::processEvents();

    auto *table = dialog.findChild<QTableWidget *>();
    QVERIFY(table);

    const int stockRow = findTeamRow(table, QStringLiteral("starter-team"));
    QVERIFY(stockRow >= 0);
    auto *stockNameCell = table->cellWidget(stockRow, 1);
    QVERIFY(stockNameCell);
    QVERIFY(stockNameCell->findChild<QLabel *>(QStringLiteral("stockBadge")));
    QVERIFY(stockNameCell->findChild<QLabel *>(QStringLiteral("defaultAgentBadge")));

    const int customRow = findTeamRow(table, QStringLiteral("custom-team"));
    QVERIFY(customRow >= 0);
    auto *customNameCell = table->cellWidget(customRow, 1);
    QVERIFY(customNameCell);
    QVERIFY(!customNameCell->findChild<QLabel *>(QStringLiteral("stockBadge")));
    QVERIFY(customNameCell->findChild<QLabel *>(QStringLiteral("defaultAgentBadge")));
}

QTEST_MAIN(TestStockItemInfo)
#include "test_stock_item_info.moc"
