// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <QObject>

namespace PhosphorEngine {

class ISnapSettings
{
public:
    virtual ~ISnapSettings() = default;

    // Global snapping master toggle. When false the user has turned snapping
    // off entirely: no window may be auto-snapped on open via any path (app
    // rule, session restore, empty-zone auto-assign, last-used-zone).
    // SnapEngine::resolveWindowRestore gates on this so the engine-internal
    // auto-snap path and the daemon-side D-Bus gate (SnapAdaptor::applySnapResult)
    // stay in lockstep.
    virtual bool snappingEnabled() const = 0;

    // excludedApplications() / excludedWindowClasses() are gone — the v4
    // migration folded those flat lists into the unified WindowRule store.
    // The daemon now wires a filtered Exclude rule set directly into the
    // SnapEngine via `setExcludeRuleSet`; consumers that previously called
    // these accessors evaluate against the rule set instead.

    virtual StickyWindowHandling snappingStickyWindowHandling() const = 0;
    virtual bool moveNewWindowsToLastZone() const = 0;
    virtual bool restoreWindowsToZonesOnLogin() const = 0;

    // When true, a window that is auto-placed into a zone on open (session
    // restore, app rule, empty-zone auto-assign, last-used-zone) is given focus.
    // Read only on the AutoRestored commit path in SnapEngine::commitSnapImpl, so
    // it never affects manual drag or keyboard snaps (those use UserInitiated).
    // Mirrors AutotileConfig::focusNewWindows. Default false.
    virtual bool focusNewWindows() const = 0;

    // Force-on master toggle: when true, every layout reaching the snap-to-
    // empty-zone path auto-assigns new windows to its first empty zone
    // regardless of its individual `autoAssign` flag. Effective behavior is
    // `globalAutoAssign OR layout->autoAssign()`. Autotile screens never
    // reach this path — they're short-circuited upstream in
    // SnapEngine::windowOpened (see lifecycle.cpp), so this flag is in
    // practice a manual-layout-only override. Default false preserves the
    // pre-#370 per-layout-only semantics.
    virtual bool autoAssignAllLayouts() const = 0;

    // When true, unfloating (Meta+F) a window that has NO pre-float zone to return
    // to — a never-snapped window that defaulted to floating — snaps it to a
    // fallback zone (last-used → first-empty → first zone in the layout) instead of
    // leaving it floating. Default false: unfloat with no pre-float zone emits OSD
    // feedback and the window stays floating. Read in SnapEngine::unfloatToZone.
    virtual bool unfloatFallbackToZone() const = 0;
};

} // namespace PhosphorEngine

// Q_DECLARE_INTERFACE is REQUIRED at moc time for any QObject that uses
// `Q_INTERFACES(PhosphorEngine::ISnapSettings)` to declare that it
// implements this interface — Settings does so in src/config/settings.h.
// Interface dispatch happens through `dynamic_cast<ISnapSettings*>` (see
// `SnapEngine::snapSettings()` in SnapEngine.cpp), not `qobject_cast`, so the
// IID string below is never exercised at runtime — but the macro pairing with
// `Q_INTERFACES` is still structurally required for an implementer to compile.
Q_DECLARE_INTERFACE(PhosphorEngine::ISnapSettings, "org.plasmazones.ISnapSettings")
