#pragma once

#include <QJsonObject>

class Template;
class Profile;

// Renders a complete opencode.json config object from a Template and Profile.
QJsonObject renderProfileToConfig(const Template &t, const Profile &p);
