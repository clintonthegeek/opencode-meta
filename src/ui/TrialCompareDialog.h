// TrialCompareDialog.h
//
// ROADMAP P2-1 — replaces the previously-inline stub in TrialsWidget
// with a real side-by-side rendered-config diff between two Trials.
//
// What it shows
// ------------
// For each of the two Trials:
//   1. A metadata header summarising id, team, project, timestamp,
//      duration, notes, and ratings.
//   2. A read-only pane with the rendered opencode.json that was
//      applied, line-by-line, with diff highlights (left tint in red,
//      right tint in green) for any line that differs between the two.
//
// Source-of-truth precedence per side
// ----------------------------------
//   * If Trial.renderedConfigSnapshot is non-empty use it directly —
//     that's the exact `opencode.json` written at apply time and is
//     the only way to faithfully reproduce what a Historical trial
//     produced even if the underlying Roles / Specialists have since
//     drifted.
//   * Otherwise, re-render the trial's current Team with the current
//     Roles + Specialists via TeamRenderer::render(); this preserves
//     usefulness for trials recorded before snapshots were written.
//   * If neither path produces JSON (no snapshot AND the Team cannot
//     be loaded), the pane shows a clear "(no rendered config
//     available)" placeholder so the diff still aligns.
//
// Visual / interaction
// --------------------
// * The dialog is purely a viewer: it does not mutate Trials, Teams,
//   or any on-disk file. Cancel / Close simply dismisses.
// * It is opened non-blocking (show() not exec()) so MainWindow stays
//   responsive and so multiple compare windows can be open at once.
//   Ownership transfers to Qt's parent-child machinery for clean-up.
//
// Diff highlighting
// -----------------
// Reuses the exact same approach as ConfirmApplyDialog.cpp:
// line-by-line equality over UTF-8 lines produced by
// QJsonDocument::Indented; differing lines get a tinted background
// (red on the left pane, green on the right pane). This visually
// matches the existing pre-apply diff in ProjectsWidget, which makes
// the application feel coherent across the two comparison flows.

#pragma once

#include <QDialog>

class StorageManager;

class TrialCompareDialog : public QDialog
{
    Q_OBJECT

public:
    // leftId / rightId : Trial ids to diff. Both must be loadable
    //                    via StorageManager::loadTrial; if either
    //                    cannot be resolved, the dialog still opens
    //                    and shows a clear placeholder, rather than
    //                    crashing the host.
    // storage          : Storage manager used to resolve the Trials
    //                    and (when needed) re-render the underlying
    //                    Teams via TeamRenderer. Read-only usage.
    // parent           : Standard Qt parent for ownership.
    TrialCompareDialog(const QString &leftId,
                       const QString &rightId,
                       StorageManager &storage,
                       QWidget *parent = nullptr);

    QString leftTrialId() const { return m_leftId; }
    QString rightTrialId() const { return m_rightId; }

    // Indented JSON shown on the left pane (snapshot preferred, then
    // re-rendered Team, then a placeholder string). Empty only when
    // both paths produced nothing — tests use this to assert
    // fallback behaviour.
    QString leftRenderedText() const { return m_leftRenderedText; }
    QString rightRenderedText() const { return m_rightRenderedText; }

private:
    QString m_leftId;
    QString m_rightId;
    QString m_leftRenderedText;
    QString m_rightRenderedText;
};
