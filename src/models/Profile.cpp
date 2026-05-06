#include "Profile.h"

Profile Profile::fromJson(const QJsonObject &obj)
{
    Profile p;

    p.id = obj.value("id").toString();
    p.name = obj.value("name").toString();
    p.templateId = obj.value("template_id").toString();

    const QJsonObject modelsObj = obj.value("model_assignments").toObject();
    for (auto it = modelsObj.begin(); it != modelsObj.end(); ++it) {
        p.modelAssignments.insert(it.key(), it.value().toString());
    }

    if (obj.contains("global_overrides") && obj.value("global_overrides").isObject()) {
        p.globalOverrides = obj.value("global_overrides").toObject();
    }

    const QJsonObject metaObj = obj.value("metadata").toObject();
    for (auto it = metaObj.begin(); it != metaObj.end(); ++it) {
        p.metadata.insert(it.key(), it.value().toString());
    }

    return p;
}

QJsonObject Profile::toJson() const
{
    QJsonObject obj;

    obj.insert("id", id);
    obj.insert("name", name);
    obj.insert("template_id", templateId);

    QJsonObject modelsObj;
    for (auto it = modelAssignments.constBegin(); it != modelAssignments.constEnd(); ++it) {
        modelsObj.insert(it.key(), it.value());
    }
    obj.insert("model_assignments", modelsObj);

    if (!globalOverrides.isEmpty()) {
        obj.insert("global_overrides", globalOverrides);
    }

    QJsonObject metaObj;
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        metaObj.insert(it.key(), it.value());
    }
    if (!metaObj.isEmpty()) {
        obj.insert("metadata", metaObj);
    }

    return obj;
}
