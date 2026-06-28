// OpencodeSchemaAdapter implementation
// Converts between internal domain models and the current opencode.json schema.
// Preserves unknown fields and isolates future schema changes.

#include "OpencodeSchemaAdapter.h"
#include "../models/Template.h"
#include "../models/Profile.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>

Template OpencodeSchemaAdapter::loadTemplate(const QJsonObject &opencodeJson)
{
    Template tpl;

    // Preserve the entire original JSON for unknown-field round-tripping
    tpl.metadata["_raw_opencode"] = QString::fromUtf8(QJsonDocument(opencodeJson).toJson(QJsonDocument::Compact));

    // Top-level name / version if present
    if (opencodeJson.contains("name")) {
        tpl.name = opencodeJson["name"].toString();
    }
    if (opencodeJson.contains("version")) {
        tpl.version = opencodeJson["version"].toString();
    }

    // Current opencode uses top-level "agent" (object of role -> agent def)
    if (opencodeJson.contains("agent") && opencodeJson["agent"].isObject()) {
        QJsonObject agentsObj = opencodeJson["agent"].toObject();
        for (auto it = agentsObj.begin(); it != agentsObj.end(); ++it) {
            if (it.value().isObject()) {
                AgentDef def = AgentDef::fromJson(it.value().toObject());
                tpl.agents.insert(it.key(), def);
            }
        }
    }

    // defaultAgent can be stored as either "default" (newer) or "default_agent" (older)
    if (opencodeJson.contains("default") && opencodeJson["default"].isString()) {
        tpl.defaultAgent = opencodeJson["default"].toString();
    } else if (opencodeJson.contains("default_agent") && opencodeJson["default_agent"].isString()) {
        tpl.defaultAgent = opencodeJson["default_agent"].toString();
    } else if (!tpl.agents.isEmpty()) {
        // pick first Primary/All agent as default if none specified
        for (auto it = tpl.agents.begin(); it != tpl.agents.end(); ++it) {
            if (it.value().mode == AgentDef::Mode::Primary || it.value().mode == AgentDef::Mode::All) {
                tpl.defaultAgent = it.key();
                break;
            }
        }
    }

    return tpl;
}

QJsonObject OpencodeSchemaAdapter::saveTemplate(const Template &tpl)
{
    QJsonObject out;
    QJsonObject originalAgents;

    // Restore any unknown fields we captured on load
    if (tpl.metadata.contains("_raw_opencode")) {
        QJsonDocument doc = QJsonDocument::fromJson(tpl.metadata["_raw_opencode"].toUtf8());
        if (doc.isObject()) {
            const QJsonObject originalRoot = doc.object();
            out = originalRoot;

            // Capture original agent objects so we can merge known fields back in
            if (originalRoot.contains("agent") && originalRoot["agent"].isObject()) {
                originalAgents = originalRoot["agent"].toObject();
            }
        }
    }

    // Ensure our known top-level keys are present / updated
    if (!tpl.name.isEmpty()) {
        out["name"] = tpl.name;
    }
    if (!tpl.version.isEmpty()) {
        out["version"] = tpl.version;
    }

    // Write agents under the current "agent" key, merging into any preserved
    // per-agent objects so nested unknown fields survive a round-trip.
    QJsonObject agentsObj;
    for (auto it = tpl.agents.begin(); it != tpl.agents.end(); ++it) {
        const QString agentName = it.key();
        const AgentDef &def = it.value();

        // Start from the original agent object if we have one, otherwise empty
        QJsonObject baseAgent = originalAgents.value(agentName).toObject();
        const QJsonObject generated = def.toJson();

        for (auto git = generated.begin(); git != generated.end(); ++git) {
            const QString key = git.key();

            // When we loaded from a real opencode.json, avoid injecting
            // legacy scalar permission flags if they were not present
            // originally. This lets newer permission objects remain the
            // single source of truth while still preserving unknown fields.
            if (!originalAgents.isEmpty()) {
                const bool isPermissionKey =
                    (key == QLatin1String("edit")) ||
                    (key == QLatin1String("bash")) ||
                    (key == QLatin1String("task")) ||
                    (key == QLatin1String("read")) ||
                    (key == QLatin1String("grep")) ||
                    (key == QLatin1String("glob"));

                if (isPermissionKey && !baseAgent.isEmpty() && !baseAgent.contains(key)) {
                    // Skip creating a new scalar permission flag that was
                    // not present in the original agent object.
                    continue;
                }
            }

            baseAgent.insert(key, git.value());
        }

        agentsObj.insert(agentName, baseAgent);
    }
    out["agent"] = agentsObj;

    if (!tpl.defaultAgent.isEmpty()) {
        // Prefer whichever key the original config used, falling back to
        // "default" for newly generated templates.
        if (out.contains("default_agent") && !out.contains("default")) {
            out["default_agent"] = tpl.defaultAgent;
        } else if (out.contains("default")) {
            out["default"] = tpl.defaultAgent;
        } else {
            out["default"] = tpl.defaultAgent;
        }
    }

    return out;
}

std::optional<QString> OpencodeSchemaAdapter::validate(const Template &tpl)
{
    if (tpl.agents.isEmpty()) {
        return QStringLiteral("Template must contain at least one agent");
    }
    if (tpl.defaultAgent.isEmpty() || !tpl.agents.contains(tpl.defaultAgent)) {
        return QStringLiteral("defaultAgent must reference an existing agent");
    }
    const AgentDef &def = tpl.agents[tpl.defaultAgent];
    if (def.mode != AgentDef::Mode::Primary && def.mode != AgentDef::Mode::All) {
        return QStringLiteral("defaultAgent must be a Primary or All agent");
    }
    return std::nullopt;
}

Profile OpencodeSchemaAdapter::loadProfile(const QJsonObject &opencodeJson)
{
    Profile p;
    if (opencodeJson.contains("_profile_meta")) {
        // future: load from our extended profile section if we embed one
    }
    // For now, a Profile is mostly derived from a Template + model assignments
    // so we leave modelAssignments empty unless we see a "model" key at top level
    if (opencodeJson.contains("model") && opencodeJson["model"].isString()) {
        // naive: assign the top-level model to a default agent if present
        if (!opencodeJson.contains("default")) {
            // no default, skip
        } else {
            QString def = opencodeJson["default"].toString();
            p.modelAssignments[def] = opencodeJson["model"].toString();
        }
    }
    return p;
}

QJsonObject OpencodeSchemaAdapter::saveProfile(const Profile &profile)
{
    QJsonObject out;
    // Minimal profile output – real implementation would merge with a Template
    if (!profile.name.isEmpty()) {
        out["name"] = profile.name;
    }
    if (!profile.modelAssignments.isEmpty()) {
        // For simplicity, write the first assignment as top-level "model"
        out["model"] = profile.modelAssignments.first();
    }
    return out;
}
