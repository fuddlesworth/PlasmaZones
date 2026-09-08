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
/// timing override (curve / duration / ...) in the config-backed
/// `Animations.MotionProfileTree`, and the event's animation SHADER assignment
/// in `Animations.ShaderProfileTree`. Both halves are set on the same card in the
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
/// @param writeOverride   Commits one entry's TIMING half into
///                        `Animations/MotionProfileTree`.
/// @param readShaders     Every direct shader override, keyed by event path
///                        (`AnimationsPageController::allRawShaderProfiles`).
///                        One call per snapshot, not one per path.
/// @param writeShader     Commits one entry's SHADER half, receiving it in the
///                        same map shape `rawShaderProfile()` returns. The
///                        closure owns the three-state dispatch (assigned pack
///                        / engaged-empty "no pack" / parameters-only over an
///                        inherited pack), because each state needs a different
///                        controller API.
/// @param resolvedShaderIds What every shader-supported path renders with,
///                         keyed by path, ancestor chain and built-in default
///                         included
///                         (`resolveShaderWithDefault(...).effectiveEffectId()`).
///                         The self-containment sweep needs the RESOLVED id
///                         rather than the built-in default: a leaf whose pack
///                         comes from a category ancestor would otherwise be
///                         captured as something the sender is not using, and —
///                         because the live snapshot it is compared against
///                         shares the same mistake — the set would still read
///                         as active while describing a different look.
///
///                         Answered for every path in ONE call, like
///                         @p readShaders and for the same reason: resolving a
///                         path rebuilds the whole ShaderProfileTree from the
///                         store, and the sweep runs on the GUI thread on every
///                         setsChanged.
/// @param knowsEffectId   Whether this build has the named pack installed.
///                        Validation refuses a set naming a pack the recipient
///                        does not have, rather than letting the write refuse
///                        it mid-batch after earlier entries already landed —
///                        the same whole-set promise the shader-leg gate keeps.
///                        Must answer true for an empty id (the "no pack"
///                        sentinel) and true while the registry is still
///                        unscanned, so an early call cannot reject everything.
ShaderSetStore::Config
makeConfig(std::function<QVariantMap()> readTimings, std::function<QString()> setsDir,
           std::function<bool(const QString& /*path*/, const QVariantMap& /*profile*/)> writeOverride,
           std::function<QVariantMap()> readShaders,
           std::function<bool(const QString& /*path*/, const QVariantMap& /*shader*/)> writeShader,
           std::function<QVariantMap()> resolvedShaderIds,
           std::function<bool(const QString& /*effectId*/)> knowsEffectId);

} // namespace PlasmaZones::motionset
