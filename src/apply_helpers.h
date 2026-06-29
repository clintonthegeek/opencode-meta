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
//   commit(targetPath, config, *catalog)
//       C0-2 (wired ahead of schedule from C0-1): same as the
//       structural-only `commit()` above, but with a live ProviderCatalog
//       passed in so every emitted `model` string is also checked against
//       the on-disk opencode-managed `<Global.Path.cache>/models.json`.
//
// `commit` is the production entry point used by StorageManager and
// by any future host (e.g. MainWindow, apply dialogs). Direct calls to
// applyConfigWithBackup are reserved for tests that want to bypass
// validation deliberately.

#pragma once

#include <QJsonObject>
#include <QString>

class ProviderCatalog;

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

// Validated write with live-catalog enforcement (D-2 / C0-2). When
// `catalog` is non-null AND loaded, every `model` string is checked
// against the live provider catalog (ContractChecker::validate(config,
// catalog)). When `catalog` is non-null but failed to load (no
// `<Global.Path.cache>/models.json` on disk, or the file could not be
// parsed), `commit` REFUSES to write the file and returns a
// success==false `ApplyResult` with an explicit "provider catalog not
// loaded" error message — no silent fallback to the structural-only
// check (the rationale is recorded in ROADMAP.md D-2: "files that
// nobody can run are useless to users"). The structural-only
// `commit(targetPath, config)` overload remains the entry point for
// callers who genuinely have no catalog (test fixtures, etc.).
ApplyResult commit(const QString &targetPath,
                   const QJsonObject &config,
                   const ProviderCatalog *catalog);
