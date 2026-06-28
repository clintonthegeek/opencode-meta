// Template and AgentDef data models
// Mirrors the Template JSON structure defined in v1spec.md

#pragma once

#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QJsonValue>

// Represents a single agent definition within a Template
class AgentDef
{
public:
    enum class Mode {
        Primary,
        Subagent,
        All
    };

    enum class Permission {
        Allow,
        Deny,
        Ask
    };

    Mode mode = Mode::Primary;
    QString description;

    // Holds either a string prompt or an object like {"file": "./relative/path.md"}
    QJsonValue prompt;

    Permission edit = Permission::Ask;
    Permission bash = Permission::Ask;
    Permission task = Permission::Ask;
    Permission read = Permission::Ask;
    Permission grep = Permission::Ask;
    Permission glob = Permission::Ask;

    // Optional fine-grained tool overrides, kept as-is from JSON
    QJsonObject tools;

    static AgentDef fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

    static QString modeToString(Mode mode);
    static Mode modeFromString(const QString &value);

    static QString permissionToString(Permission permission);
    static Permission permissionFromString(const QString &value);
};

// Top-level template definition
class Template
{
public:
    QString id;
    QString name;
    QString version;

    // Map of agent role name -> AgentDef
    QMap<QString, AgentDef> agents;

    // Name of the default agent (must be a primary agent)
    QString defaultAgent;

    // Generic metadata map, typically contains "created" and "modified" ISO timestamps
    QMap<QString, QString> metadata;

    static Template fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};
