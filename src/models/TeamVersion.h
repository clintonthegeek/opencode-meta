// TeamVersion data model
//
// Mirrors a single timestamped snapshot of a Team for the ROADMAP P3-3
// "Team version history" feature. A TeamVersion is a *parent chain node*
// that captures the exact contents of a Team at the moment it was saved,
// plus enough metadata to timeline the changes:
//
//   * id                -- stable snapshot id, used as the on-disk
//                          filename stem under team-versions/<id>.json
//   * teamId            -- which Team this snapshot belongs to
//   * parentVersionId   -- the snapshot immediately before this one,
//                          or empty for the first snapshot
//   * timestampUtc      -- when the snapshot was taken
//   * reason            -- short tag for the cause:
//                          "auto-edit"     user-driven editor change
//                          "auto-create"   Team was just created
//                          "revert:<id>"   this snapshot is the result
//                                          of reverting to <id>
//   * note              -- optional free-form note a human can attach
//   * team              -- the snapshot's Team payload (full Team record)
//
// Snapshots are append-only. The current Team file at teams/<id>.json
// is always the live, most-recent state; older states are reached
// exclusively through `team-versions/<id>.json`.

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include "models/Team.h"

class TeamVersion
{
public:
    QString id;
    QString teamId;
    QString parentVersionId;
    QDateTime timestampUtc;
    QString reason;
    QString note;

    Team team;

    static TeamVersion fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

    // Reason tags so call sites and tests can string-match without
    // risking typos against the in-code QString literals.
    constexpr static const char *kReasonAutoEdit   = "auto-edit";
    constexpr static const char *kReasonAutoCreate = "auto-create";
    constexpr static const char *kReasonRevert     = "revert";
};
