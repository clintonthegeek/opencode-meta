#include "generation/AgentMarkdown.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "models/Role.h"
#include "models/Specialist.h"

// Anonymous-namespace helpers — kept out of the header so that the
// runtime (TeamRenderer / opencode-meta-qt executable) doesn't pull in
// any of the YAML conversion helpers unless it actually uses them.
namespace {

// Emit a v2 `permissions` Ruleset array (matching report §6.2):
//   Rule = { action, resource, effect }
//   Ruleset = Rule[]
QJsonArray renderRuleset(const QJsonObject &v1Permission)
{
    QJsonArray out;
    for (auto it = v1Permission.constBegin(); it != v1Permission.constEnd(); ++it) {
        const QString &action = it.key();
        const QJsonValue v = it.value();
        if (v.isString()) {
            QJsonObject rule;
            rule.insert(QStringLiteral("action"), action);
            rule.insert(QStringLiteral("resource"), QStringLiteral("*"));
            rule.insert(QStringLiteral("effect"), v.toString());
            out.append(rule);
            continue;
        }
        if (v.isObject()) {
            const QJsonObject pat = v.toObject();
            for (auto pIt = pat.constBegin(); pIt != pat.constEnd(); ++pIt) {
                QJsonObject rule;
                rule.insert(QStringLiteral("action"), action);
                rule.insert(QStringLiteral("resource"), pIt.key());
                const QJsonValue ev = pIt.value();
                if (ev.isString()) {
                    rule.insert(QStringLiteral("effect"), ev.toString());
                }
                out.append(rule);
            }
        }
    }
    return out;
}

// Escape a single-line string for safe inclusion as YAML scalar value.
// We keep it simple: replace ASCII double-quote with backslash-double-
// quote + return the value wrapped in double quotes. Multi-line strings
// are written as YAML block scalars (`|`) so the user's newlines are
// preserved verbatim.
QString jsonStringToYamlScalar(const QString &s, bool forceBlock = false)
{
    if (!forceBlock && !s.contains(QLatin1Char('\n'))
        && !s.contains(QLatin1Char('"'))
        && !s.contains(QLatin1Char('\\'))) {
        // Single safe line: emit as plain string.
        return s;
    }
    const QString escaped = QString(s).replace(QStringLiteral("\\"),
                                               QStringLiteral("\\\\"))
                                       .replace(QStringLiteral("\""),
                                               QStringLiteral("\\\""));
    if (forceBlock || s.contains(QLatin1Char('\n'))) {
        QString out = QStringLiteral("|\n");
        const QStringList lines = s.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            out += QStringLiteral("  ") + line + QStringLiteral("\n");
        }
        return out;
    }
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

// Render a single YAML line for a string-keyed scalar (frontmatter
// field). Returns the line as "<key>: <value>" without a trailing
// newline — caller adds the newline.
QString yamlLine(const QString &key, const QString &value, bool scalarIsBlock = false)
{
    return key + QStringLiteral(": ") + jsonStringToYamlScalar(value, scalarIsBlock);
}

QString yamlBoolLine(const QString &key, bool value)
{
    return key + QStringLiteral(": ") + (value ? QStringLiteral("true")
                                              : QStringLiteral("false"));
}

} // namespace

QString AgentMarkdown::render(const Specialist &specialist, const Role &role)
{
    QString out;
    out += QStringLiteral("---\n");

    // description (v2 frontmatter key — PARADIGM §5.5 / report §7.2)
    if (!role.description.isEmpty()) {
        out += yamlLine(QStringLiteral("description"), role.description) + QStringLiteral("\n");
    }

    // model — pulled from the Specialist's concrete binding
    if (!specialist.modelId.isEmpty()) {
        out += yamlLine(QStringLiteral("model"), specialist.modelId) + QStringLiteral("\n");
    }

    // mode — mirrors the Role's mode + the v1→v2 mapping
    out += yamlLine(QStringLiteral("mode"), Role::modeToString(role.mode)) + QStringLiteral("\n");

    // hidden — only emitted when set explicitly (no Role field today,
    // so keep this def-off in v0.1 unless RoleEditorDialog surfaces it)
    {
        const QJsonObject md = role.metadata;
        if (md.contains(QStringLiteral("hidden"))
            && md.value(QStringLiteral("hidden")).isBool()) {
            out += yamlBoolLine(QStringLiteral("hidden"),
                                md.value(QStringLiteral("hidden")).toBool())
                   + QStringLiteral("\n");
        }
    }

    // color — only emitted when present (Role.metadata.color or future
    // Role.color field). PARADIGM §2.1 doesn't currently model color
    // on Role, so this is forward-compatible only.
    {
        const QJsonObject md = role.metadata;
        if (md.contains(QStringLiteral("color"))
            && md.value(QStringLiteral("color")).isString()) {
            out += yamlLine(QStringLiteral("color"),
                            md.value(QStringLiteral("color")).toString())
                   + QStringLiteral("\n");
        }
    }

    // steps — only emitted when present (Role.metadata.steps or future
    // Role.steps field).
    {
        const QJsonObject md = role.metadata;
        if (md.contains(QStringLiteral("steps"))
            && md.value(QStringLiteral("steps")).isDouble()) {
            out += yamlLine(QStringLiteral("steps"),
                            QString::number(md.value(QStringLiteral("steps")).toInt()))
                   + QStringLiteral("\n");
        }
    }

    // variant — pulled from Role.metadata.variant if present (forward-
    // compatibility; not currently modeled on Role).
    {
        const QJsonObject md = role.metadata;
        if (md.contains(QStringLiteral("variant"))
            && md.value(QStringLiteral("variant")).isString()) {
            out += yamlLine(QStringLiteral("variant"),
                            md.value(QStringLiteral("variant")).toString())
                   + QStringLiteral("\n");
        }
    }

    // system — mirrors Role.systemPrompt + Specialist.promptOverride
    // (same resolution rule TeamRenderer uses).
    QString systemBody;
    if (!specialist.promptOverride.isUndefined() && !specialist.promptOverride.isNull()) {
        systemBody = specialist.promptOverride.toString();
    } else if (!role.systemPrompt.isUndefined() && !role.systemPrompt.isNull()) {
        systemBody = role.systemPrompt.toString();
    }
    if (!systemBody.isEmpty()) {
        const bool isMultiLine = systemBody.contains(QLatin1Char('\n'));
        out += yamlLine(QStringLiteral("system"), systemBody, isMultiLine)
               + (isMultiLine ? QString() : QStringLiteral("\n"));
    }

    // request — only emitted when present (forward-compat; not currently
    // modeled on Role).
    {
        const QJsonObject md = role.metadata;
        if (md.contains(QStringLiteral("request"))) {
            const QJsonValue v = md.value(QStringLiteral("request"));
            if (v.isBool()) {
                out += yamlBoolLine(QStringLiteral("request"), v.toBool())
                       + QStringLiteral("\n");
            } else if (v.isString()) {
                out += yamlLine(QStringLiteral("request"), v.toString())
                       + QStringLiteral("\n");
            }
        }
    }

    // permissions — flattened Ruleset array (v2 shape).
    if (!role.permissions.isEmpty()) {
        const QJsonArray arr = renderRuleset(role.permissions);
        out += QStringLiteral("permissions:\n");
        for (const QJsonValue &v : arr) {
            const QJsonObject rule = v.toObject();
            const QString action = rule.value(QStringLiteral("action")).toString();
            const QString resource = rule.value(QStringLiteral("resource")).toString();
            const QString effect = rule.value(QStringLiteral("effect")).toString();
            out += QStringLiteral("  - action: ") + jsonStringToYamlScalar(action) + QStringLiteral("\n");
            out += QStringLiteral("    resource: ") + jsonStringToYamlScalar(resource) + QStringLiteral("\n");
            out += QStringLiteral("    effect: ") + jsonStringToYamlScalar(effect) + QStringLiteral("\n");
        }
    }

    // disabled — mirrors v1 `disable`. Today neither Role nor
    // Specialist carries an explicit disable field, so this is
    // omitted by default; the future-on Role.metadata.disabled
    // round-trip is intentional and documented.
    {
        const QJsonObject md = role.metadata;
        if (md.contains(QStringLiteral("disabled"))
            && md.value(QStringLiteral("disabled")).isBool()) {
            out += yamlBoolLine(QStringLiteral("disabled"),
                                md.value(QStringLiteral("disabled")).toBool())
                   + QStringLiteral("\n");
        }
    }

    out += QStringLiteral("---\n");

    // Body: the unrendered prompt (kept verbatim so doc-style hosting
    // shows the full text). Empty body is OK — a leading newline after
    // the closing `---` gives a clean blank line.
    if (!systemBody.isEmpty()) {
        out += QStringLiteral("\n");
        out += systemBody;
        if (!systemBody.endsWith(QLatin1Char('\n'))) {
            out += QStringLiteral("\n");
        }
    }

    return out;
}
