// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineTypes/EngineTypes.h>
#include <QObject>
#include <QStringList>

namespace PhosphorEngineApi {

class ISnapSettings
{
public:
    virtual ~ISnapSettings() = default;

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

} // namespace PhosphorEngineApi

Q_DECLARE_INTERFACE(PhosphorEngineApi::ISnapSettings, "org.plasmazones.ISnapSettings")
