// tests/test_agent_markdown.cpp
// Phase C5-1 / D-8: AgentMarkdown.render(specialist, role) emits a
// v2-frontmatter `.md` body. Per ROADMAP D-8, `.md` emission is
// DEFERRED to after C0-C4 — the module is scaffolded + tested here so
// future flipping of the toggle is mechanical. The smoke-trio regex
// (`scripts/ci_smoke_trio.sh`) now includes this test name as part of
// the D-8 reversal (see ROADMAP §2 C5 status note).
//
// What we assert:
//   * frontmatter delimiters (`---`) book-end the YAML block;
//   * every frontmatter key round-trips: model, mode, description,
//     system, permissions (Ruleset shape), hidden, color, steps, variant,
//     request, disabled;
//   * body uses Specialist.promptOverride over Role.systemPrompt when
//     both are set, mirroring TeamRenderer's resolution rule;
//   * missing keys are omitted (no zero-value pollution);
//   * multi-line system prompts are emitted as YAML block scalars.

#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "generation/AgentMarkdown.h"
#include "models/Role.h"
#include "models/Specialist.h"

class TestAgentMarkdown : public QObject
{
    Q_OBJECT

private slots:
    void frontmatterDelimitersArePresent();
    void everyKeyRoundTrips();
    void bodyUsesPromptOverride();
    void missingKeysAreOmitted();
    void multilineSystemIsBlockScalar();
    void permissionsAreRulesetArray();
    void hiddenColorStepsVariantRequestDisabledPassThrough();
};

namespace {

// Build a fully-populated Specialist + Role pair so every key has a
// source to round-trip. Mirrors the shape we'd see if the user has
// toggled the "Also write agent `.md` files" settings flag.
Role fullyPopulatedRole()
{
    Role role;
    role.id = QStringLiteral("with-md");
    role.description = QStringLiteral("Primary agent with `.md` round-trip coverage");
    role.systemPrompt = QJsonValue(QStringLiteral("Default role body — should be replaced by the Specialist's promptOverride."));
    role.mode = Role::Mode::Primary;
    QJsonObject perms;
    perms.insert(QStringLiteral("edit"), QStringLiteral("ask"));
    perms.insert(QStringLiteral("bash"), QStringLiteral("ask"));
    perms.insert(QStringLiteral("read"), QStringLiteral("allow"));
    role.permissions = perms;

    QJsonObject md;
    md.insert(QStringLiteral("hidden"), QJsonValue(true));
    md.insert(QStringLiteral("color"), QStringLiteral("#a0a0ff"));
    md.insert(QStringLiteral("steps"), QJsonValue(7));
    md.insert(QStringLiteral("variant"), QStringLiteral("high-context"));
    md.insert(QStringLiteral("request"), QJsonValue(true));
    md.insert(QStringLiteral("disabled"), QJsonValue(false));
    role.metadata = md;
    return role;
}

Specialist fullyPopulatedSpecialist()
{
    Specialist s;
    s.id = QStringLiteral("md-spec");
    s.roleId = QStringLiteral("with-md");
    s.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    s.name = QStringLiteral("MD Spec");
    s.promptOverride = QJsonValue(QStringLiteral("This is the override body."));

    // The v2 form ALSO supports file-reference prompts; exercise both:
    QJsonObject md;
    md.insert(QStringLiteral("file_reference"), QStringLiteral("./prompts/with-md.md"));
    s.metadata = md;
    return s;
}

} // namespace

void TestAgentMarkdown::frontmatterDelimitersArePresent()
{
    const Role role = fullyPopulatedRole();
    const Specialist spec = fullyPopulatedSpecialist();
    const QString out = AgentMarkdown::render(spec, role);

    QVERIFY2(out.startsWith(QStringLiteral("---\n")),
             "rendered `.md` MUST start with the YAML `---` delimiter");
    QVERIFY2(out.contains(QStringLiteral("\n---\n")),
             "rendered `.md` MUST include a closing `---` delimiter on "
             "its own line");
}

void TestAgentMarkdown::everyKeyRoundTrips()
{
    const Role role = fullyPopulatedRole();
    const Specialist spec = fullyPopulatedSpecialist();
    const QString out = AgentMarkdown::render(spec, role);

    // Slice out the frontmatter block: between the first "---" and the
    // second "---".
    const int openIdx = out.indexOf(QStringLiteral("---\n"));
    QVERIFY(openIdx == 0);
    const int closeIdx = out.indexOf(QStringLiteral("\n---\n"), openIdx + 4);
    QVERIFY(closeIdx > 0);
    const QString fm = out.mid(openIdx + 4, closeIdx - openIdx - 4);

    QVERIFY2(fm.contains(QStringLiteral("description: ")),
             "frontmatter MUST contain description key");
    QVERIFY2(fm.contains(QStringLiteral("model: anthropic/claude-sonnet-4-6")),
             "frontmatter MUST contain the Specialist's modelId");
    QVERIFY2(fm.contains(QStringLiteral("mode: primary")),
             "frontmatter MUST contain the role's mode");
    QVERIFY2(fm.contains(QStringLiteral("system: ")),
             "frontmatter MUST contain a system key (mirrors v1 prompt)");
    QVERIFY2(fm.contains(QStringLiteral("permissions:")),
             "frontmatter MUST contain a permissions Ruleset array");
    QVERIFY2(fm.contains(QStringLiteral("- action: edit"))
             || fm.contains(QStringLiteral("- action: bash"))
             || fm.contains(QStringLiteral("- action: read")),
             "permissions Ruleset MUST list each rule as a YAML dash entry");
}

void TestAgentMarkdown::bodyUsesPromptOverride()
{
    const Role role = fullyPopulatedRole();
    const Specialist spec = fullyPopulatedSpecialist();

    const QString out = AgentMarkdown::render(spec, role);
    // The Specialist's promptOverride should win over the Role's
    // systemPrompt. We assert by checking that the override body text
    // appears AFTER the closing `---` (the v2 body region) and the
    // Role's default body is NOT in the body section.
    const int closeIdx = out.indexOf(QStringLiteral("\n---\n"));
    QVERIFY(closeIdx > 0);
    const QString body = out.mid(closeIdx + 5); // skip past "\n---\n"
    QVERIFY2(body.contains(QStringLiteral("This is the override body.")),
             "body MUST carry the Specialist's promptOverride text");
    QVERIFY2(!body.contains(QStringLiteral("Default role body")),
             "body MUST NOT carry the Role's systemPrompt when the "
             "Specialist has a promptOverride");
}

void TestAgentMarkdown::missingKeysAreOmitted()
{
    // Strip the metadata-driven keys (hidden, color, steps, variant,
    // request, disabled) and the description; the renderer should
    // omit them entirely from the output.
    Role role = fullyPopulatedRole();
    Specialist spec = fullyPopulatedSpecialist();
    role.description.clear();
    role.metadata = QJsonObject(); // clear metadata

    // Permissions also cleared — verify permissions: is omitted.
    role.permissions = QJsonObject();

    // systemPrompt + promptOverride empty so the system: key is also
    // omitted.
    role.systemPrompt = QJsonValue();
    spec.promptOverride = QJsonValue();

    const QString out = AgentMarkdown::render(spec, role);
    const int openIdx = out.indexOf(QStringLiteral("---\n"));
    QVERIFY(openIdx == 0);
    const int closeIdx = out.indexOf(QStringLiteral("\n---\n"), openIdx + 4);
    QVERIFY(closeIdx > 0);
    const QString fm = out.mid(openIdx + 4, closeIdx - openIdx - 4);

    QVERIFY2(!fm.contains(QStringLiteral("description:")),
             "empty description MUST NOT emit a description line");
    QVERIFY2(!fm.contains(QStringLiteral("hidden:")),
             "no metadata.hidden MUST NOT emit a hidden line");
    QVERIFY2(!fm.contains(QStringLiteral("color:")),
             "no metadata.color MUST NOT emit a color line");
    QVERIFY2(!fm.contains(QStringLiteral("steps:")),
             "no metadata.steps MUST NOT emit a steps line");
    QVERIFY2(!fm.contains(QStringLiteral("variant:")),
             "no metadata.variant MUST NOT emit a variant line");
    QVERIFY2(!fm.contains(QStringLiteral("request:")),
             "no metadata.request MUST NOT emit a request line");
    QVERIFY2(!fm.contains(QStringLiteral("disabled:")),
             "no metadata.disabled MUST NOT emit a disabled line");
    QVERIFY2(!fm.contains(QStringLiteral("system:")),
             "no systemPrompt + no promptOverride MUST NOT emit a system line");
    QVERIFY2(!fm.contains(QStringLiteral("permissions:")),
             "empty permissions MUST NOT emit a permissions line");
}

void TestAgentMarkdown::multilineSystemIsBlockScalar()
{
    // A multi-line system prompt should be emitted as YAML block
    // scalar (`|`) rather than a single-line escaped string. Tests
    // the user-facing scenario of an editor that saved a prompt with
    // formatting whitespace the runtime should respect verbatim.
    Role role;
    role.systemPrompt = QJsonValue(QStringLiteral("Line 1\nLine 2\nLine 3"));
    role.mode = Role::Mode::Primary;

    Specialist spec;
    spec.roleId = role.id;
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    // No promptOverride → systemPrompt wins.
    spec.promptOverride = QJsonValue();

    const QString out = AgentMarkdown::render(spec, role);
    QVERIFY2(out.contains(QStringLiteral("system: |\n")),
             qPrintable(QStringLiteral(
                 "multi-line system prompt MUST be emitted as block "
                 "scalar; got: %1").arg(out)));
    QVERIFY2(out.contains(QStringLiteral("Line 1")),
             "block scalar MUST contain the original line text verbatim");
    QVERIFY2(out.contains(QStringLiteral("Line 2")),
             "block scalar MUST contain the original line text verbatim");
    QVERIFY2(out.contains(QStringLiteral("Line 3")),
             "block scalar MUST contain the original line text verbatim");
}

void TestAgentMarkdown::permissionsAreRulesetArray()
{
    // Each entry in v1 `permission` becomes a Ruleset entry in v2
    // `permissions`. Verify that the rules carry action / resource /
    // effect on their own lines (matching report §6.2).
    Role role;
    role.systemPrompt = QJsonValue(QStringLiteral("x"));
    role.mode = Role::Mode::Primary;

    QJsonObject perms;
    perms.insert(QStringLiteral("edit"), QStringLiteral("ask"));
    perms.insert(QStringLiteral("bash"), QStringLiteral("deny"));
    role.permissions = perms;

    Specialist spec;
    spec.roleId = role.id;
    spec.modelId = QStringLiteral("anthropic/claude-sonnet-4-6");
    spec.promptOverride = QJsonValue(QStringLiteral("x"));

    const QString out = AgentMarkdown::render(spec, role);
    QVERIFY2(out.contains(QStringLiteral("permissions:\n")),
             "permissions MUST be a YAML sequence under `permissions:`");
    QVERIFY2(out.contains(QStringLiteral("- action: edit")),
             "permissions MUST list each rule's action");
    QVERIFY2(out.contains(QStringLiteral("action: edit")),
             "permissions MUST emit action label for `edit`");
    QVERIFY2(out.contains(QStringLiteral("action: bash")),
             "permissions MUST emit action label for `bash`");
    QVERIFY2(out.contains(QStringLiteral("resource: *")),
             "flat Action form MUST emit `resource: *` to match Ruleset shape");
    QVERIFY2(out.contains(QStringLiteral("effect: ask")),
             "permissions MUST carry the effect value");
    QVERIFY2(out.contains(QStringLiteral("effect: deny")),
             "permissions MUST carry the deny effect value");
}

void TestAgentMarkdown::hiddenColorStepsVariantRequestDisabledPassThrough()
{
    // Phase C5 verify: metadata.hidden / metadata.color /
    // metadata.steps / metadata.variant / metadata.request /
    // metadata.disabled all flow through to the v2 frontmatter when
    // present. (These are NOT currently modeled on Role directly;
    // metadata carries them until PARADIGM §2.1 is extended.)
    const Role role = fullyPopulatedRole();
    const Specialist spec = fullyPopulatedSpecialist();
    const QString out = AgentMarkdown::render(spec, role);

    QVERIFY2(out.contains(QStringLiteral("hidden: true")),
             "metadata.hidden MUST emit `hidden: true` when set");
    QVERIFY2(out.contains(QStringLiteral("color: #a0a0ff"))
             || out.contains(QStringLiteral("color: \"#a0a0ff\"")),
             qPrintable(QStringLiteral(
                 "metadata.color MUST emit `color: #a0a0ff` (plain or "
                 "quoted); got frontmatter:\n%1").arg(out)));
    QVERIFY2(out.contains(QStringLiteral("steps: 7")),
             "metadata.steps MUST emit `steps: 7`");
    QVERIFY2(out.contains(QStringLiteral("variant: high-context"))
             || out.contains(QStringLiteral("variant: \"high-context\"")),
             qPrintable(QStringLiteral(
                 "metadata.variant MUST emit `variant: high-context` (plain "
                 "or quoted); got frontmatter:\n%1").arg(out)));
    QVERIFY2(out.contains(QStringLiteral("request: true")),
             "metadata.request (bool) MUST emit `request: true`");
    QVERIFY2(out.contains(QStringLiteral("disabled: false")),
             "metadata.disabled (bool) MUST emit `disabled: false`");
}

QTEST_MAIN(TestAgentMarkdown)
#include "test_agent_markdown.moc"
