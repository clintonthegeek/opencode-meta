// OpencodeSchemaAdapter
// Converts between internal domain models (Template, Profile) and current opencode.json schema.
// Designed to preserve unknown fields and isolate future schema changes from the UI.

#pragma once

#include <QString>
#include <QJsonObject>
#include <optional>

class Template;
class Profile;

class OpencodeSchemaAdapter
{
public:
    // Load a raw opencode.json into an internal Template representation.
    // Unknown fields are preserved inside the returned Template's metadata or a shadow object.
    static Template loadTemplate(const QJsonObject &opencodeJson);

    // Convert an internal Template back to opencode.json, merging any preserved unknown fields.
    static QJsonObject saveTemplate(const Template &tpl);

    // Basic validation: returns error message if invalid, empty optional if valid.
    static std::optional<QString> validate(const Template &tpl);

    // Profile loading/saving (profiles are typically embedded or derived).
    static Profile loadProfile(const QJsonObject &opencodeJson);
    static QJsonObject saveProfile(const Profile &profile);
};
