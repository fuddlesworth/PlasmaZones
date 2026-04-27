// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <memory>

namespace PhosphorAnimation {
class CurveLoader;
class CurveRegistry;
class ProfileLoader;
} // namespace PhosphorAnimation

namespace PlasmaZones {

/// Owns the per-process CurveRegistry + ProfileLoader + CurveLoader that
/// populate `PhosphorAnimation::PhosphorProfileRegistry::instance()` with
/// the shipped profile/curve JSONs.
///
/// The phosphor-animation profile registry is a process-local Meyers
/// singleton, and `PhosphorMotionAnimation { profile: "..." }` resolves
/// against that singleton. The daemon owns its own ProfileLoader (see
/// daemon.cpp) which feeds its in-process registry; secondary processes
/// (settings, editor) need their own loader to bootstrap the registry —
/// otherwise every QML profile lookup misses and the animation falls
/// back to the library-default 150 ms OutCubic, regardless of what the
/// shipped profile JSON says.
///
/// Construct one of these in main() before loading QML and keep it alive
/// for the lifetime of the application. The destructor unwires the
/// QFileSystemWatcher live-reload paths cleanly.
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

private:
    std::unique_ptr<PhosphorAnimation::CurveRegistry> m_curveRegistry;
    std::unique_ptr<PhosphorAnimation::CurveLoader> m_curveLoader;
    std::unique_ptr<PhosphorAnimation::ProfileLoader> m_profileLoader;
};

} // namespace PlasmaZones
