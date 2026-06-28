// apply_helpers.h
// Helpers for safely writing rendered opencode.json configs to disk
// with timestamped backups. Kept in the core library so behavior can
// be exercised from tests without pulling in UI code.

#pragma once

#include <QJsonObject>
#include <QString>

struct ApplyResult
{
    bool success = false;
    QString backupPath;   // empty if no prior file existed
    QString errorString;  // non-empty on failure
};

// Write a rendered config object to the given target path.
//
// Behavior:
// - Ensures the parent directory exists (creates it if needed).
// - If a file already exists at targetPath, creates a timestamped
//   backup alongside it: targetPath.YYYYMMDDHHMMSS.bak
// - Writes the new config via a temporary file and then renames it
//   into place.
// - On failure, attempts to leave the original file + backup in
//   place; errorString is populated.
ApplyResult applyConfigWithBackup(const QString &targetPath,
                                  const QJsonObject &config);
