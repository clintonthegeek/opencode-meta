// Role data model
// Mirrors the Role entity defined in docs/PARADIGM.md (v0.1).

#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonValue>

class Role
{
public:
    enum class Mode {
        Primary,
        Subagent,
        All
    };

    QString id;           // Stable identifier for this role
    QString name;         // Human-readable name (e.g. "Coder", "Planner")
    QString description;  // Long-form description

    // Holds either a string prompt or an object like {"file": "./relative/path.md"}
    // Field name in JSON: system_prompt (see PARADIGM.md v0.1 Role.systemPrompt)
    QJsonValue systemPrompt;

    Mode mode = Mode::Primary;  // primary / subagent / all

    // Phase C3-1 / D-5: mark the role as read-only. Combined with
    // `mode == Primary` this lets the renderer cleanly omit `edit` /
    // `bash` permission keys (report §6.4 second-class fix: weak-model
    // tool-call failures). Default false. The flag is NOT consulted
    // when `mode == Subagent` / `All` — subagents may legitimately
    // need narrow write access.
    bool readOnly = false;

    // Raw permission/tool/metadata objects, kept flexible per PARADIGM.md.
    QJsonObject permissions;    // e.g. { "edit": "ask", "bash": "deny", ... }
    QJsonObject tools;          // Optional fine-grained tool overrides
    QJsonObject metadata;       // Arbitrary metadata (tags, notes, timestamps, ...)

    static Role fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

    static QString modeToString(Mode mode);
    static Mode modeFromString(const QString &value);
};
