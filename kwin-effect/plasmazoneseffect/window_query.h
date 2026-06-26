// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorWindowRules/WindowQuery.h>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

/// Map KWin's overlapping window-type predicates onto exactly one
/// PhosphorProtocol::WindowType. Single source of truth for both the rule
/// engine query (windowRuleQueryFor below) AND the daemon's window metadata
/// push (window_identity.cpp::pushWindowMetadata casts the enum to its
/// underlying int for D-Bus transport). Ordered most-specific-first because a
/// window can satisfy several predicates at once (a modal dialog is both a
/// dialog and modal — modality is orthogonal state, deliberately not a
/// WindowType).
PhosphorProtocol::WindowType windowTypeFor(KWin::EffectWindow* w);

/// Build a per-window PhosphorWindowRules::WindowQuery from a live KWin window,
/// populating every window-side field declared on `WindowQuery` so user-
/// authored rules can match on any of them. The unified shape means a rule
/// that passes the rule-override gate (hasAnyMatch) also resolves its slot
/// in the per-event animation cascade.
///
/// Engagement contract (which fields get filled):
///   - **Engaged on non-empty value** (`std::optional` stays disengaged when
///     the source is empty so `Equals ""` / `Regex "^$"` foot-guns don't
///     silently match): `appId`, `windowClass`, `title`, `windowRole`,
///     `desktopFile`, `pid` (gated on `pid > 0` — KWin returns 0 for
///     Wayland surfaces with no attached process and X11 windows missing
///     `_NET_WM_PID`).
///   - **Always engaged** when @p w is non-null: `windowType` (returns
///     `Unknown` if no specific predicate matches — distinguishable from
///     other types by a `WindowType Equals Unknown` predicate);
///     `isMinimized`, `isFullscreen`, `isSticky`. `isMaximized` is engaged
///     only when the underlying `KWin::Window*` exists (EffectWindow has
///     no direct maximized accessor).
///   - **Context fields** `screenId` / `virtualDesktop` / `activity`: these
///     ARE matchable `MatchExpression` fields, so a window-domain rule may
///     legitimately pin them (e.g. "no title bar + red border on monitor 2",
///     "zero opacity on activity X"). `virtualDesktop` / `activity` are derived
///     from the window itself (first desktop's x11 number; first activity UUID;
///     0 / empty meaning all/unknown — matching the daemon-side
///     `setWindowMetadata` derivation). `screenId` requires the effect's
///     output→stable-id resolution, which is not available to this free
///     helper, so the caller passes it via @p screenId (typically
///     `getWindowScreenId(w)`). Unlike the window string fields, `screenId` is a
///     non-optional context field that is always present; passing it empty
///     resolves as the empty/unknown screen value (the all/unknown convention),
///     not a disengaged optional. NOTE: a window
///     query carrying a populated context only ENABLES context-pinned
///     window-domain rules to match — it does not affect the windowless
///     context cascade, which routes through `ContextRuleBridge`.
///
/// PlasmaZones placement state (@p isFloating / @p isSnapped / @p zoneId) is
/// supplied by the caller because it lives in the effect's runtime caches
/// (NavigationHandler), not on the KWin window. The caller passes
/// `isWindowFloating(wid)`, `isWindowSnapped(wid)`, `zoneForWindow(wid)`. These
/// are REQUIRED (no defaults) so every call site is compiler-forced to supply
/// them — a site that silently omitted them would leave IsFloating / IsSnapped /
/// Zone unmatched in that resolver path only.
///
/// Confined to the effect translation unit so the LGPL phosphor-window-rules
/// library never sees a KWin type.
PhosphorWindowRules::WindowQuery windowRuleQueryFor(KWin::EffectWindow* w, const QString& screenId, bool isFloating,
                                                    bool isSnapped, bool isTiled, const QString& zoneId);

} // namespace PlasmaZones
