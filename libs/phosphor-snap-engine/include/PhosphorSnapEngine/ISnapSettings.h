// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <QObject>
#include <QStringList>

namespace PhosphorEngine {

class ISnapSettings
{
public:
    virtual ~ISnapSettings() = default;

    // Global snapping master toggle. When false the user has turned snapping
    // off entirely: no window may be auto-snapped on open via any path (app
    // rule, session restore, empty-zone auto-assign, last-used-zone). The
    // engine's auto-snap resolver gates on this so the daemon-side D-Bus gate
    // and this engine-internal one (SnapEngine::windowOpened) stay in lockstep.
    virtual bool snappingEnabled() const = 0;

    virtual QStringList excludedApplications() const = 0;
    virtual QStringList excludedWindowClasses() const = 0;
    virtual StickyWindowHandling stickyWindowHandling() const = 0;
    virtual bool moveNewWindowsToLastZone() const = 0;
    virtual bool restoreWindowsToZonesOnLogin() const = 0;

    // Force-on master toggle: when true, every layout reaching the snap-to-
    // empty-zone path auto-assigns new windows to its first empty zone
    // regardless of its individual `autoAssign` flag. Effective behavior is
    // `globalAutoAssign OR layout->autoAssign()`. Autotile screens never
    // reach this path — they're short-circuited upstream in
    // SnapEngine::windowOpened (see lifecycle.cpp), so this flag is in
    // practice a manual-layout-only override. Default false preserves the
    // pre-#370 per-layout-only semantics.
    virtual bool autoAssignAllLayouts() const = 0;
};

} // namespace PhosphorEngine

Q_DECLARE_INTERFACE(PhosphorEngine::ISnapSettings, "org.plasmazones.ISnapSettings")
