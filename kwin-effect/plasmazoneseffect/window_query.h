// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorWindowRule/WindowQuery.h>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

/// Build a per-window PhosphorWindowRule::WindowQuery from a live KWin window,
/// populating every window-side field declared on `WindowQuery` so user-
/// authored rules can match on any of them (AppId / WindowClass / Title /
/// WindowRole / DesktopFile / WindowType / Pid / Sticky / Fullscreen /
/// Minimized / Maximized). Context fields (screenId / virtualDesktop /
/// activity) are left at their defaults — the call sites here are window-
/// scoped consumers (exclusion gates, animation rule-override gates,
/// per-window animation resolvers) that match on attributes of the window
/// itself, not the desktop / activity context it currently lives on.
///
/// Confined to the effect translation unit so the LGPL phosphor-windowrule
/// library never sees a KWin type — the unified store of all callers means
/// the gate sites and the resolver sites match on the same field set, so a
/// rule that passes the gate also resolves its slot.
PhosphorWindowRule::WindowQuery windowRuleQueryFor(KWin::EffectWindow* w);

} // namespace PlasmaZones
