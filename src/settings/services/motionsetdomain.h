// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "settings/stores/shadersetstore.h"

#include <QString>
#include <QVariantMap>

#include <functional>

namespace PlasmaZones::motionset {

/// Build the motion-set domain configuration for a ShaderSetStore.
///
/// A motion set captures the WHOLE per-event unit, which is two stores: the
/// timing override FILE in the profiles directory (curve / duration / ...) and
/// the event's animation SHADER assignment, which lives in the config-backed
/// `Animations.ShaderProfileTree`. Both halves are set on the same card in the
/// Animations UI, so a set that carried only one of them captured half of what
/// the user sees as one thing — and left the decoration domain, whose single
/// tree carries pack ids and parameters together, doing strictly more.
///
/// On-disk shape per entry, format 2:
/// ```
/// { "path": "window.appearance.open",
///   "profile": { "curve": …, "duration": …,
///                "shader": { "effectId": …, "parameters": { … } } } }
/// ```
/// The timing fields stay at the top of `profile` exactly where format 1 put
/// them, and the shader half is one new nested key. A format-1 set therefore
/// still reads correctly and simply assigns no shader. The version was bumped
/// anyway because the reverse direction matters more: a format-2 set opened by
/// an older build would drop the shader half on parse and silently apply a
/// half-set, which is the exact failure the store's version gate exists to
/// turn into a clean refusal.
///
/// The domain never reaches across the controller boundary itself: timing
/// writes go through @p writeOverride (wired to
/// `AnimationsPageController::setOverride`) and shader writes through
/// @p writeShader. Both halves land in config, which is why this signature
/// carries no file-staging hooks — the same shape the decoration domain has
/// always had.
///
/// @param readTimings     The stored per-event TIMING tree
///                        (`ISettings::motionProfileTree`), read once per
///                        snapshot.
/// @param setsDir         Absolute path of the motion-sets directory.
/// @param writeOverride   Commits one entry's TIMING half as a per-path file.
/// @param readShaders     Every direct shader override, keyed by event path
///                        (`AnimationsPageController::allRawShaderProfiles`).
///                        One call per snapshot, not one per path.
/// @param writeShader     Commits one entry's SHADER half, receiving it in the
///                        same map shape `rawShaderProfile()` returns. The
///                        closure owns the three-state dispatch (assigned pack
///                        / engaged-empty "no pack" / parameters-only over an
///                        inherited pack), because each state needs a different
///                        controller API.
ShaderSetStore::Config
makeConfig(std::function<QVariantMap()> readTimings, std::function<QString()> setsDir,
           std::function<bool(const QString& /*path*/, const QVariantMap& /*profile*/)> writeOverride,
           std::function<QVariantMap()> readShaders,
           std::function<bool(const QString& /*path*/, const QVariantMap& /*shader*/)> writeShader);

} // namespace PlasmaZones::motionset
