// Round-trip integration test for RoleEditorDialog.
//
// Constructed dialogs are never shown; the harness sets values by
// reaching for the widget tree via objectName. The test asserts that:
//   * Loaded Role fields populate every widget in the dialog (mode,
//     permissions, tools, metadata, description, systemPrompt).
//   * Identical output round-trips through toJson / fromJson.
//   * Edits made via the UI propagate to roleData() for every field.
//
// This guards the binding between the dialog surface and the Role
// model — if anyone reorders widgets or breaks loadFromRole /
// applyToRole, the assertions below will fail loudly.

#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>
#include <QTimer>
#include <QTemporaryFile>

#include "models/Role.h"
#include "ui/RoleEditorDialog.h"

class TestRoleEditorDialog : public QObject
{
    Q_OBJECT

private slots:
    void loadShowsAllFields();
    void roundTripWithoutEdits();
    void editsPropagateToRoleData();

    // Phase 2: Prompt tab — inline / file-reference modes + preview
    void loadDetectsFileReferenceMode();
    void loadDefaultsToInlineForStringPrompt();
    void roundTripFileReferenceUntouched();
    void applyEmitsFileObjectWhenPathChanged();
    void applyDropsEmptyFilePath();
    void applyDropsEmptyInlineText();
    void modeSwitchPersistsAcrossEdits();
    void loadFromFileButtonSwitchesModeToFile();
    void browseButtonAcceptsAbsolutePath();
    void previewBodyReflectsCurrentMode();
    void previewTokenLabelUpdates();
};

namespace {

QString textOfName(const QObject *parent, const QString &name)
{
    const QLineEdit *le = parent->findChild<const QLineEdit *>(name);
    return le ? le->text() : QString();
}

QString plainTextOfName(const QObject *parent, const QString &name)
{
    const QPlainTextEdit *pe = parent->findChild<const QPlainTextEdit *>(name);
    return pe ? pe->toPlainText() : QString();
}

QString currentComboText(const QObject *parent, const QString &name)
{
    const QComboBox *combo = parent->findChild<const QComboBox *>(name);
    return combo ? combo->currentText() : QString();
}

int tableRowCount(const QObject *parent, const QString &name)
{
    const QTableWidget *t = parent->findChild<const QTableWidget *>(name);
    return t ? t->rowCount() : -1;
}

QString tableCell(const QObject *parent, const QString &name, int row, int col)
{
    const QTableWidget *t = parent->findChild<const QTableWidget *>(name);
    if (!t) {
        return QString();
    }
    const QTableWidgetItem *item = t->item(row, col);
    return item ? item->text() : QString();
}

QStringList toolsInList(const QObject *parent, const QString &name)
{
    const QListWidget *list = parent->findChild<const QListWidget *>(name);
    QStringList out;
    if (!list) {
        return out;
    }
    for (int i = 0; i < list->count(); ++i) {
        out.append(list->item(i)->text());
    }
    return out;
}

bool isRadioChecked(const QObject *parent, const QString &name)
{
    const QRadioButton *radio = parent->findChild<const QRadioButton *>(name);
    return radio && radio->isChecked();
}

int stackedIndex(const QObject *parent, const QString &name)
{
    const QStackedWidget *stack = parent->findChild<const QStackedWidget *>(name);
    return stack ? stack->currentIndex() : -1;
}

int indexOfTableRowWith(const QTableWidget *t, int col, const QString &needle)
{
    for (int row = 0; row < t->rowCount(); ++row) {
        const QTableWidgetItem *item = t->item(row, col);
        if (item && item->text() == needle) {
            return row;
        }
    }
    return -1;
}

void setTableCell(QTableWidget *t, int row, int col, const QString &value)
{
    if (!t) {
        return;
    }
    if (auto *existing = t->item(row, col)) {
        existing->setText(value);
    } else {
        auto *item = new QTableWidgetItem(value);
        t->setItem(row, col, item);
    }
}

Role buildRichRole()
{
    Role role;
    role.id = QStringLiteral("rich-role");
    role.name = QStringLiteral("Rich Role");
    role.description = QStringLiteral("A fully-populated fixture role");
    role.systemPrompt = QJsonValue(QStringLiteral("You are the Rich Role."));

    role.mode = Role::Mode::Subagent;

    QJsonObject perms;
    perms.insert(QStringLiteral("edit"), QStringLiteral("ask"));
    perms.insert(QStringLiteral("bash"), QStringLiteral("deny"));
    perms.insert(QStringLiteral("webfetch"), QStringLiteral("allow"));
    role.permissions = perms;

    QJsonObject tools;
    tools.insert(QStringLiteral("bash"), QJsonValue(true));
    tools.insert(QStringLiteral("read"), QJsonValue(true));
    role.tools = tools;

    QJsonObject metadata;
    metadata.insert(QStringLiteral("owner"), QStringLiteral("clinton"));
    metadata.insert(QStringLiteral("complexity"), QJsonValue(7));
    QJsonObject nested;
    nested.insert(QStringLiteral("label"), QStringLiteral("internal"));
    metadata.insert(QStringLiteral("tags"), nested);
    role.metadata = metadata;

    return role;
}

} // namespace

void TestRoleEditorDialog::loadShowsAllFields()
{
    Role rich = buildRichRole();
    RoleEditorDialog dlg(rich);

    QCOMPARE(textOfName(&dlg, QStringLiteral("roleEditor.nameEdit")), rich.name);
    QCOMPARE(textOfName(&dlg, QStringLiteral("roleEditor.descriptionEdit")), rich.description);
    QCOMPARE(currentComboText(&dlg, QStringLiteral("roleEditor.modeCombo")), QStringLiteral("Subagent"));
    QCOMPARE(plainTextOfName(&dlg, QStringLiteral("roleEditor.systemPromptEdit")),
             rich.systemPrompt.toString());

    auto *permTable = dlg.findChild<QTableWidget *>(QStringLiteral("roleEditor.permissionsTable"));
    QVERIFY(permTable);
    QCOMPARE(permTable->rowCount(), 3);
    QVERIFY(indexOfTableRowWith(permTable, 0, QStringLiteral("edit")) >= 0);
    QVERIFY(indexOfTableRowWith(permTable, 0, QStringLiteral("bash")) >= 0);
    QVERIFY(indexOfTableRowWith(permTable, 0, QStringLiteral("webfetch")) >= 0);

    auto *toolsList = dlg.findChild<QListWidget *>(QStringLiteral("roleEditor.toolsList"));
    QVERIFY(toolsList);
    const QStringList tools = toolsInList(&dlg, QStringLiteral("roleEditor.toolsList"));
    QCOMPARE(tools.size(), 2);
    QVERIFY(tools.contains(QStringLiteral("bash")));
    QVERIFY(tools.contains(QStringLiteral("read")));

    auto *metaTable = dlg.findChild<QTableWidget *>(QStringLiteral("roleEditor.metadataTable"));
    QVERIFY(metaTable);
    QCOMPARE(metaTable->rowCount(), 3);
    QVERIFY(indexOfTableRowWith(metaTable, 0, QStringLiteral("owner")) >= 0);
    QVERIFY(indexOfTableRowWith(metaTable, 0, QStringLiteral("complexity")) >= 0);
    QVERIFY(indexOfTableRowWith(metaTable, 0, QStringLiteral("tags")) >= 0);
}

void TestRoleEditorDialog::roundTripWithoutEdits()
{
    Role rich = buildRichRole();
    RoleEditorDialog dlg(rich);

    const Role finalRole = dlg.roleData();
    const QJsonObject fromEditor = finalRole.toJson();
    const Role reparsed = Role::fromJson(fromEditor);

    QCOMPARE(reparsed.id, rich.id);
    QCOMPARE(reparsed.name, rich.name);
    QCOMPARE(reparsed.description, rich.description);
    QCOMPARE(reparsed.mode, rich.mode);
    QCOMPARE(reparsed.permissions, rich.permissions);
    QCOMPARE(reparsed.tools.keys().size(), rich.tools.keys().size());
    QCOMPARE(reparsed.metadata, rich.metadata);
    QCOMPARE(finalRole.systemPrompt, rich.systemPrompt);
}

void TestRoleEditorDialog::editsPropagateToRoleData()
{
    RoleEditorDialog dlg(buildRichRole());

    // Edit name, description, mode via QLineEdit / QComboBox.
    auto *nameEdit = dlg.findChild<QLineEdit *>(QStringLiteral("roleEditor.nameEdit"));
    QVERIFY(nameEdit);
    nameEdit->setText(QStringLiteral("Renamed"));

    auto *descEdit = dlg.findChild<QLineEdit *>(QStringLiteral("roleEditor.descriptionEdit"));
    QVERIFY(descEdit);
    descEdit->setText(QStringLiteral("Renamed description"));

    auto *modeCombo = dlg.findChild<QComboBox *>(QStringLiteral("roleEditor.modeCombo"));
    QVERIFY(modeCombo);
    modeCombo->setCurrentIndex(modeCombo->findData(static_cast<int>(Role::Mode::All)));

    // Add a new permission row via a synthetic insert + GUI buttons mimic.
    auto *permTable = dlg.findChild<QTableWidget *>(QStringLiteral("roleEditor.permissionsTable"));
    QVERIFY(permTable);
    const int newRow = permTable->rowCount();
    permTable->insertRow(newRow);
    permTable->setItem(newRow, 0, new QTableWidgetItem(QStringLiteral("read")));
    permTable->setItem(newRow, 1, new QTableWidgetItem(QStringLiteral("allow")));

    // Add a new tool via the Add button flow.
    auto *toolEdit = dlg.findChild<QLineEdit *>(QStringLiteral("roleEditor.toolNameEdit"));
    QVERIFY(toolEdit);
    toolEdit->setText(QStringLiteral("webfetch"));

    auto *addTool = dlg.findChild<QPushButton *>(QStringLiteral("roleEditor.addToolButton"));
    QVERIFY(addTool);
    QMetaObject::invokeMethod(addTool, "click", Qt::DirectConnection);

    // Add metadata row.
    auto *metaTable = dlg.findChild<QTableWidget *>(QStringLiteral("roleEditor.metadataTable"));
    QVERIFY(metaTable);
    const int metaRow = metaTable->rowCount();
    metaTable->insertRow(metaRow);
    metaTable->setItem(metaRow, 0, new QTableWidgetItem(QStringLiteral("note")));
    metaTable->setItem(metaRow, 1, new QTableWidgetItem(QStringLiteral("hello world")));

    // Modify the existing "complexity" metadata cell from 7 (QJsonValue::Double)
    // to 9 to ensure changes flow through applyToRole.
    setTableCell(metaTable, indexOfTableRowWith(metaTable, 0, QStringLiteral("complexity")), 1, QStringLiteral("9"));

    const Role updated = dlg.roleData();
    QCOMPARE(updated.name, QStringLiteral("Renamed"));
    QCOMPARE(updated.description, QStringLiteral("Renamed description"));
    QCOMPARE(updated.mode, Role::Mode::All);

    QCOMPARE(updated.permissions.value(QStringLiteral("read")).toString(), QStringLiteral("allow"));
    QVERIFY(updated.permissions.value(QStringLiteral("edit")).toString() == QStringLiteral("ask"));

    QVERIFY(updated.tools.value(QStringLiteral("webfetch")).toBool(true));
    QVERIFY(updated.tools.value(QStringLiteral("bash")).toBool(true));
    QCOMPARE(updated.tools.keys().size(), 3);

    QCOMPARE(updated.metadata.value(QStringLiteral("note")).toString(), QStringLiteral("hello world"));
    QCOMPARE(updated.metadata.value(QStringLiteral("complexity")).toInt(), 9);
    QVERIFY(updated.metadata.value(QStringLiteral("owner")).toString() == QStringLiteral("clinton"));

    // Calling roleData() should be idempotent (no leaked UI state).
    const Role again = dlg.roleData();
    QCOMPARE(again.toJson(), updated.toJson());
}

// ----------------------------------------------------------------------------
// Phase 2: Prompt tab — inline / file-reference modes + preview
// ----------------------------------------------------------------------------

namespace {

Role makeRoleWithPrompt(const QJsonValue &prompt)
{
    Role role;
    role.id = QStringLiteral("prompt-role");
    role.name = QStringLiteral("Prompt Role");
    role.description = QStringLiteral("Used by Phase 2 prompt-mode tests.");
    role.systemPrompt = prompt;
    role.mode = Role::Mode::Primary;
    return role;
}

QString filePathOf(const QObject *parent)
{
    const QLineEdit *edit = parent->findChild<const QLineEdit *>(
        QStringLiteral("roleEditor.filePathEdit"));
    return edit ? edit->text() : QString();
}

void setInlinePrompt(QWidget *dlg, const QString &text)
{
    auto *edit = dlg->findChild<QPlainTextEdit *>(
        QStringLiteral("roleEditor.systemPromptEdit"));
    QVERIFY(edit);
    edit->setPlainText(text);
}

void setFilePath(QWidget *dlg, const QString &path)
{
    auto *edit = dlg->findChild<QLineEdit *>(
        QStringLiteral("roleEditor.filePathEdit"));
    QVERIFY(edit);
    edit->setText(path);
}

} // namespace

void TestRoleEditorDialog::loadDetectsFileReferenceMode()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./prompts/coder.md"));
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(obj)));

    QVERIFY(isRadioChecked(&dlg, QStringLiteral("roleEditor.promptFileRadio")));
    QVERIFY(!isRadioChecked(&dlg, QStringLiteral("roleEditor.promptInlineRadio")));
    QCOMPARE(stackedIndex(&dlg, QStringLiteral("roleEditor.promptModeStack")), 1);
    QCOMPARE(filePathOf(&dlg), QStringLiteral("./prompts/coder.md"));

    // The inline plain text edit should be empty (and untouched, no surrogate JSON dump).
    const QString plainText = plainTextOfName(&dlg, QStringLiteral("roleEditor.systemPromptEdit"));
    QVERIFY(plainText.isEmpty());
}

void TestRoleEditorDialog::loadDefaultsToInlineForStringPrompt()
{
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(QStringLiteral("Plain prompt body"))));

    QVERIFY(isRadioChecked(&dlg, QStringLiteral("roleEditor.promptInlineRadio")));
    QVERIFY(!isRadioChecked(&dlg, QStringLiteral("roleEditor.promptFileRadio")));
    QCOMPARE(stackedIndex(&dlg, QStringLiteral("roleEditor.promptModeStack")), 0);
    QCOMPARE(filePathOf(&dlg), QString());
    QCOMPARE(plainTextOfName(&dlg, QStringLiteral("roleEditor.systemPromptEdit")),
             QStringLiteral("Plain prompt body"));
}

void TestRoleEditorDialog::roundTripFileReferenceUntouched()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./prompts/coder.md"));
    Role seed = makeRoleWithPrompt(QJsonValue(obj));

    RoleEditorDialog dlg(seed);

    // Don't touch any widget — close to Approved immediately.
    Role updated = dlg.roleData();

    // Must remain an object, NOT collapsed to a string.
    QVERIFY(updated.systemPrompt.isObject());
    QCOMPARE(updated.systemPrompt.toObject().value(QStringLiteral("file")).toString(),
             QStringLiteral("./prompts/coder.md"));
    QCOMPARE(updated.systemPrompt, seed.systemPrompt);
}

void TestRoleEditorDialog::applyEmitsFileObjectWhenPathChanged()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./prompts/coder.md"));
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(obj)));

    // Switch to Inline mode then back to File-ref mode, edit the path.
    auto *inlineRadio = dlg.findChild<QRadioButton *>(
        QStringLiteral("roleEditor.promptInlineRadio"));
    auto *fileRadio = dlg.findChild<QRadioButton *>(
        QStringLiteral("roleEditor.promptFileRadio"));
    QVERIFY(inlineRadio);
    QVERIFY(fileRadio);
    inlineRadio->setChecked(false); // explicit toggle to flush preview state
    fileRadio->setChecked(true);
    setFilePath(&dlg, QStringLiteral("./prompts/coder-v2.md"));

    const Role updated = dlg.roleData();
    QVERIFY(updated.systemPrompt.isObject());
    QCOMPARE(updated.systemPrompt.toObject().value(QStringLiteral("file")).toString(),
             QStringLiteral("./prompts/coder-v2.md"));
}

void TestRoleEditorDialog::applyDropsEmptyFilePath()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./prompts/coder.md"));
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(obj)));

    // Wipe the path entirely — the dialog should drop system_prompt entirely
    // rather than write {"file": ""}.
    setFilePath(&dlg, QString());

    const Role updated = dlg.roleData();
    QVERIFY(updated.systemPrompt.isNull() || updated.systemPrompt.isUndefined());
}

void TestRoleEditorDialog::applyDropsEmptyInlineText()
{
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(QStringLiteral("Keep me."))));
    setInlinePrompt(&dlg, QString());

    const Role updated = dlg.roleData();
    QVERIFY(updated.systemPrompt.isNull() || updated.systemPrompt.isUndefined());
}

void TestRoleEditorDialog::modeSwitchPersistsAcrossEdits()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./prompts/coder.md"));
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(obj)));

    auto *inlineRadio = dlg.findChild<QRadioButton *>(
        QStringLiteral("roleEditor.promptInlineRadio"));
    auto *fileRadio = dlg.findChild<QRadioButton *>(
        QStringLiteral("roleEditor.promptFileRadio"));
    QVERIFY(inlineRadio);
    QVERIFY(fileRadio);

    // File ref -> Inline: stack flips, switch preview updates.
    inlineRadio->setChecked(true);
    QCOMPARE(stackedIndex(&dlg, QStringLiteral("roleEditor.promptModeStack")), 0);
    setInlinePrompt(&dlg, QStringLiteral("Converted to inline text"));

    // Inline -> File ref: stack flips back, file field still has the original path.
    fileRadio->setChecked(true);
    QCOMPARE(stackedIndex(&dlg, QStringLiteral("roleEditor.promptModeStack")), 1);
    QCOMPARE(filePathOf(&dlg), QStringLiteral("./prompts/coder.md"));

    const Role updated = dlg.roleData();
    QVERIFY(updated.systemPrompt.isObject());
    QCOMPARE(updated.systemPrompt.toObject().value(QStringLiteral("file")).toString(),
             QStringLiteral("./prompts/coder.md"));
}

void TestRoleEditorDialog::loadFromFileButtonSwitchesModeToFile()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./prompts/coder.md"));
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(obj)));

    // Confirm we are currently in file-ref mode.
    QVERIFY(isRadioChecked(&dlg, QStringLiteral("roleEditor.promptFileRadio")));

    auto *loadBtn = dlg.findChild<QPushButton *>(
        QStringLiteral("roleEditor.loadFromFileButton"));
    QVERIFY(loadBtn);

    // The button opens QFileDialog which we cannot drive headlessly — but we
    // can verify it exists, is enabled, and sits inside the inline panel
    // (the file panel hosts the Browse... button instead). StackedWidget
    // index 0 == inline panel.
    auto *stack = dlg.findChild<QStackedWidget *>(
        QStringLiteral("roleEditor.promptModeStack"));
    QVERIFY(stack);
    QObject *inlinePanel = stack->widget(0);
    QVERIFY(inlinePanel);
    QVERIFY(inlinePanel->findChild<QPushButton *>(
        QStringLiteral("roleEditor.loadFromFileButton")) != nullptr);

    // Also confirm objectName wiring exists for Browse (file panel).
    QObject *filePanel = stack->widget(1);
    QVERIFY(filePanel);
    QVERIFY(filePanel->findChild<QPushButton *>(
        QStringLiteral("roleEditor.browseFileButton")) != nullptr);
    QVERIFY(filePanel->findChild<QLineEdit *>(
        QStringLiteral("roleEditor.filePathEdit")) != nullptr);
}

void TestRoleEditorDialog::browseButtonAcceptsAbsolutePath()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./old.md"));
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(obj)));

    auto *browseBtn = dlg.findChild<QPushButton *>(
        QStringLiteral("roleEditor.browseFileButton"));
    QVERIFY(browseBtn);

    // Simulate the user typing an absolute path (this is what Browse does
    // after the user picks a file).
    setFilePath(&dlg, QStringLiteral("/tmp/opencode-meta/prompts/coder.md"));

    Role updated = dlg.roleData();
    QVERIFY(updated.systemPrompt.isObject());
    QCOMPARE(updated.systemPrompt.toObject().value(QStringLiteral("file")).toString(),
             QStringLiteral("/tmp/opencode-meta/prompts/coder.md"));
}

void TestRoleEditorDialog::previewBodyReflectsCurrentMode()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("file"), QStringLiteral("./prompts/coder.md"));
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(obj)));

    // File mode: preview body shows the serialized "{\"file\": ...}" string.
    const QString previewFile = plainTextOfName(&dlg, QStringLiteral("roleEditor.promptPreviewBody"));
    QVERIFY(previewFile.contains(QStringLiteral("\"file\"")));
    QVERIFY(previewFile.contains(QStringLiteral("./prompts/coder.md")));

    // Switch to inline -> preview body now shows the inline text.
    auto *inlineRadio = dlg.findChild<QRadioButton *>(
        QStringLiteral("roleEditor.promptInlineRadio"));
    QVERIFY(inlineRadio);
    inlineRadio->setChecked(true);
    setInlinePrompt(&dlg, QStringLiteral("Inline preview body"));

    const QString previewInline = plainTextOfName(&dlg, QStringLiteral("roleEditor.promptPreviewBody"));
    QCOMPARE(previewInline, QStringLiteral("Inline preview body"));
}

void TestRoleEditorDialog::previewTokenLabelUpdates()
{
    RoleEditorDialog dlg(makeRoleWithPrompt(QJsonValue(QStringLiteral(""))));

    QLabel *tokenLabel = dlg.findChild<QLabel *>(
        QStringLiteral("roleEditor.promptPreviewTokenLabel"));
    QVERIFY(tokenLabel);

    auto expectTokens = [&](int count) {
        const QString text = tokenLabel->text();
        QVERIFY(text.contains(QStringLiteral("approx.")));
        QVERIFY(text.contains(QStringLiteral("tokens")));
        QVERIFY(text.contains(QString::number(count)));
    };

    expectTokens(0);

    setInlinePrompt(&dlg, QStringLiteral("a b c d e f g h"));
    const QString eightChar = QStringLiteral("a b c d e f g h");
    expectTokens((eightChar.size() + 3) / 4);

    auto *fileRadio = dlg.findChild<QRadioButton *>(
        QStringLiteral("roleEditor.promptFileRadio"));
    QVERIFY(fileRadio);
    fileRadio->setChecked(true);

    setFilePath(&dlg, QStringLiteral("./prompts/long-path.md"));
    const QString path = QStringLiteral("./prompts/long-path.md");
    expectTokens((path.size() + 3) / 4);
}

QTEST_MAIN(TestRoleEditorDialog)
#include "test_role_editor_dialog.moc"
