// apply_helpers.h
// Helpers for safely writing rendered opencode.json configs to disk
// with timestamped backups. Kept in the core library so behavior can
// be exercised from tests without pulling in UI code.
//
// Two-layer API:
//
//   applyConfigWithBackup(targetPath, config)
//       Pure file-IO: creates backup if the target exists, writes a
//       temp file, then renames into place. No validation.
//
//   commit(targetPath, config)
//       Validates `config` through ContractChecker (report §12.3)
//       BEFORE any IO. If validation fails, returns an ApplyResult
//       with success==false and the validator errors chained into
//       errorString — no file or backup is created. Only after the
//       pre-write gate does it delegate to applyConfigWithBackup().
//
// `commit` is the production entry point used by StorageManager and
// by any future host (e.g. MainWindow, apply dialogs). Direct calls to
// applyConfigWithBackup are reserved for tests that want to bypass
// validation deliberately.

#pragma once

#include <QJsonObject>
#include <QString>

struct ApplyResult
{
    bool success = false;
    QString backupPath;   // empty if no prior file existed
    QString errorString;  // non-empty on failure
};

// Pure file-IO write (no validation).
ApplyResult applyConfigWithBackup(const QString &targetPath,
                                  const QJsonObject &config);

// Validated write. Runs ContractChecker::validate(config) first.
// Short-circuits with success==false if the contract is violated.
ApplyResult commit(const QString &targetPath,
                   const QJsonObject &config);
