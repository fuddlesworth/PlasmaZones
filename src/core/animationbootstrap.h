// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <memory>

namespace PhosphorAnimation {
class CurveLoader;
class CurveRegistry;
class ProfileLoader;
} // namespace PhosphorAnimation

namespace PlasmaZones {

/// Owns the per-process CurveRegistry, PhosphorProfileRegistry, and the
/// loaders that populate them from shipped + user JSONs. The
/// composition root that constructs an AnimationBootstrap is
/// responsible for publishing the registries via their respective
/// `setDefaultRegistry` calls (`PhosphorCurve::setDefaultRegistry`,
/// `PhosphorProfileRegistry::setDefaultRegistry`,
/// `QtQuickClockManager::setDefaultManager`) — those publications live
/// in the composition-root code (editor's main.cpp, settings's main.cpp,
/// or the daemon's setupAnimationProfiles) rather than here, because
/// the QML-side handles (`PhosphorCurve` / `QtQuickClockManager`) live
/// in the QML module which `plasmazones_core` does not link against.
///
/// Each composition root constructs one of these in `main()` before
/// loading QML and keeps it alive for the application lifetime. Use
/// the `profileRegistry()` / `curveRegistry()` accessors to thread the
/// owned registries into other services or to publish them.
///
/// The daemon owns equivalent wiring directly in `Daemon` (where the
/// registries are full-fat members alongside the rest of the daemon's
/// services); `AnimationBootstrap` is the lightweight shape for
/// processes that don't need the rest of the daemon machinery.
///
/// This is the PlasmaZones-flavoured wrapper — it scans
/// `${XDG_DATA_DIRS}/plasmazones/{curves,profiles}` and the user-writable
/// equivalents, mirroring the daemon. Library-level loaders stay
/// consumer-agnostic per Phase-4 decision U.
class PLASMAZONES_EXPORT AnimationBootstrap
{
public:
    AnimationBootstrap();
    ~AnimationBootstrap();

    AnimationBootstrap(const AnimationBootstrap&) = delete;
    AnimationBootstrap& operator=(const AnimationBootstrap&) = delete;

    /// Borrowed accessors for callers that need to thread the same
    /// registry into other services in the composition root, or to
    /// publish the registry pointers to QML via the static-default
    /// handles. The `AnimationBootstrap` instance must outlive any
    /// borrow.
    PhosphorAnimation::PhosphorProfileRegistry* profileRegistry()
    {
        return &m_profileRegistry;
    }
    PhosphorAnimation::CurveRegistry* curveRegistry()
    {
        return m_curveRegistry.get();
    }

private:
    std::unique_ptr<PhosphorAnimation::CurveRegistry> m_curveRegistry;
    PhosphorAnimation::PhosphorProfileRegistry m_profileRegistry;
    std::unique_ptr<PhosphorAnimation::CurveLoader> m_curveLoader;
    std::unique_ptr<PhosphorAnimation::ProfileLoader> m_profileLoader;
};

} // namespace PlasmaZones
