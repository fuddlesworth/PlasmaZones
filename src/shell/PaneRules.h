// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRules/Rule.h>

#include <QObject>
#include <QString>
#include <QUuid>

namespace PhosphorShellApp::PaneRules {

// The shell's bundled window rules for its engine-placed panes (A2 §4.2).
//
// Rules live in the daemon's rules.json, which the daemon alone writes and
// which nothing seeds from data/ any more (the managed appearance baselines
// moved to the config store). So the shell, as the owner of the panes,
// carries the rule set for them and installs it over org.plasmazones.Rules
// on startup: one MANAGED rule per pane, with a stable id derived from the
// pane's app id, added only when absent. Managed rules sit at lowest
// precedence and stay out of the Rules page, so a user rule for the same
// app id wins and nothing of the shell's shows up as user-authored.
//
// What the rule can and cannot say, against A2 §4.2:
//   snapping   SnapToZone by ordinal. "The zone nearest the chip" is not a
//              rule vocabulary; zone 1 is the closest fixed choice.
//   tiling     A2 wants the pane inserted as a tile at End. The tiling
//              insert position is a PER-CONTEXT slot (ActionSlot::
//              InsertPosition, matched on screen/desktop, not on the
//              window), so a window rule cannot express it; the pane takes
//              the screen's configured insert position. Its minimum size
//              is carried by the toplevel itself.
//   scrolling  OpenColumnWidth at the 1/3 preset and OpenColumnPlacement
//              newColumn. "After the focused column" is the screen's
//              insert position (rightOfActive by default), which is also
//              per-context.

/// The rule id for a pane app id: a v5 UUID, so the same pane always seeds
/// the same rule and a second startup finds it present.
[[nodiscard]] QUuid ruleIdFor(const QString& appId);

/// The control center's rule, complete and valid against the action
/// registry. `appId` is the pane's Wayland app id.
[[nodiscard]] PhosphorRules::Rule controlCenterRule(const QString& appId);

/// Install `rule` into the daemon's store if no rule with its id exists.
/// Asynchronous and best-effort: a daemon that is not running gets nothing
/// (the pane then falls back to floating anyway) and a later start of the
/// shell tries again. `parent` scopes the pending calls.
void seed(QObject* parent, const PhosphorRules::Rule& rule);

} // namespace PhosphorShellApp::PaneRules
