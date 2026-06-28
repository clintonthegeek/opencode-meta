#pragma once

#include <QJsonObject>
#include <QStringList>

class Template;
class Profile;

// Renders a complete opencode.json config object from a Template and Profile.
QJsonObject renderProfileToConfig(const Template &t, const Profile &p);

// Produce a lightweight, top-level summary of differences between two
// rendered opencode.json configs. Intended for UI compare workflows and
// tests without pulling in heavy diffing dependencies.
//
// The summary focuses on which top-level keys differ, are only present on
// one side, or are identical. Nested structures are treated as a single
// complex value per key.
QStringList summarizeTopLevelConfigDiff(const QJsonObject &left, const QJsonObject &right);
