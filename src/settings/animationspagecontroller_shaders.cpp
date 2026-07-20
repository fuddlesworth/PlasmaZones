// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Shader-leg methods for AnimationsPageController:
//   * Available-shader enumeration (availableShaderEffects,
//     availableShaderEffectsForPath, shaderParameters,
//     supportsShaderLeg).
//   * User shader directory + shader-pack install
//     (userShaderDirectoryPath, openUserShaderDirectory, installShaderPack).
//   * Per-event shader read + override (rawShaderProfile,
//     resolvedShaderProfile, setShaderOverride, clearShaderOverride,
//     shaderOverrideDescendantCount, clearShaderOverrideDescendants,
//     shaderEffectUsages).
//
// All methods are members of PlasmaZones::AnimationsPageController and use its
// private state — same class as animationspagecontroller.cpp, separate
// translation unit, no API change.

#include "animationspagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/animationshadersupportedpaths.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../phosphor_i18n.h"
#include "animationfileutils.h"
#include "animations_controller_detail.h"
#include "shaderpackinstaller.h"

// IMPORTANT: include via the project-local path (PhosphorAnimation/),
// not the system PhosphorAnimationShaders/ path. There are two copies
// of this header in the dependency graph — the project's local one
// and a system-installed companion — and unity-build batches multiple
// TUs into one, so mixing the two paths produces "redefinition of
// struct" errors. The sibling animationspagecontroller.cpp picks the
// same path.
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace PlasmaZones {

// Bring the shared effect/parameter/profile→QVariantMap helpers from
// animations_controller_detail.h into scope so the call sites below stay
// unqualified — matches the sibling main TU's using-declaration.
using namespace animations_controller_detail;

bool AnimationsPageController::supportsShaderLeg(const QString& path) const
{
    return eventPathSupportsShaderLeg(path);
}

QVariantList AnimationsPageController::availableShaderEffects() const
{
    QVariantList result;
    if (!m_shaderRegistry)
        return result;
    const auto effects = m_shaderRegistry->availableEffects();
    result.reserve(effects.size());
    for (const auto& effect : effects)
        result.append(effectToMap(effect));
    return result;
}

QVariantList AnimationsPageController::availableShaderEffectsForPath(const QString& path) const
{
    QVariantList result;
    if (!m_shaderRegistry)
        return result;

    const auto effects = m_shaderRegistry->availableEffects();
    result.reserve(effects.size());
    for (const auto& effect : effects) {
        // Only offer shaders whose contract matches this event's class — a
        // geometry-morph on an appearance leg, or a window shader on a desktop
        // switch, would silently no-op. Filtering (rather than dimming) keeps
        // each picker to one coherent set: appearance shaders on the Appearance
        // page, the morph shaders on Movement, the drag-physics packs on the
        // "Dragged" leaf, the two-texture packs on Virtual Desktops. `dimmed`
        // is retained (always false) for QML compatibility.
        if (!PhosphorAnimationShaders::shaderEffectAppliesToEventPath(effect, path)) {
            continue;
        }
        QVariantMap m = effectToMap(effect);
        m.insert(QLatin1String("dimmed"), false);
        m.insert(QLatin1String("dimReason"), QString());
        result.append(m);
    }
    return result;
}

QVariantList AnimationsPageController::shaderParameters(const QString& effectId) const
{
    if (!m_shaderRegistry || effectId.isEmpty() || !m_shaderRegistry->hasEffect(effectId))
        return {};
    const auto effect = m_shaderRegistry->effect(effectId);
    QVariantList result;
    result.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters)
        result.append(parameterInfoToMap(p));
    return result;
}

QString AnimationsPageController::userShaderDirectoryPath() const
{
    // cleanPath normalises away any stray double-slash from a future
    // ConfigDefaults::userAnimationsSubdir() refactor that forgets to
    // strip the leading `/` — passing a `//plasmazones/animations`
    // path to QDir / QDesktopServices on some setups surfaces as
    // confusing "directory not found" downstream.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userAnimationsSubdir());
}

void AnimationsPageController::openUserShaderDirectory()
{
    const QString dir = userShaderDirectoryPath();
    if (!QDir().mkpath(dir)) {
        // mkpath failure here is rare (XDG dir not writable, full
        // partition, etc.) but a silent swallow leaves the user with
        // an "Open Folder" button that opens nothing — surface the
        // reason via the chrome toast so the user knows to look at the
        // file manager / disk permissions.
        qCWarning(lcConfig) << "openUserShaderDirectory: mkpath failed for" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the user shader directory."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

bool AnimationsPageController::installShaderPack(const QString& sourceUrl)
{
    // All validation + copy lives in the shared ShaderPackInstaller
    // helper (src/settings/shaderpackinstaller.{h,cpp}). The snapping-
    // shaders page uses the same primitive; the security-sensitive
    // bits (symlink rejection, metadata.json verification, rollback)
    // only need an audit in one place.
    const auto result = ShaderPackInstaller::install(sourceUrl, userShaderDirectoryPath());
    if (result != ShaderPackInstaller::Result::Success) {
        const QString message = ShaderPackInstaller::errorMessage(result);
        qCWarning(lcConfig) << "installShaderPack:" << message << "— source:" << sourceUrl;
        // Surface the reason to the user via the chrome's toast — the
        // QML drop handler shows a generic InlineMessage, but the actual
        // error (DestinationExists, MissingMetadata, PackTooLarge…) is
        // useful diagnostic context that would otherwise be lost to
        // journalctl.
        Q_EMIT toastRequested(message);
        return false;
    }
    // The registry's filewatcher rescans on its own — no explicit poke
    // needed. If a poke is ever required, emit `shaderEffectsChanged`
    // here.
    return true;
}

QVariantMap AnimationsPageController::rawShaderProfile(const QString& path) const
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return {};
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    if (!tree.hasOverride(path))
        return {};
    return shaderProfileToMap(tree.directOverride(path));
}

QVariantMap AnimationsPageController::resolvedShaderProfile(const QString& path) const
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return {};
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    // resolveShaderWithDefault (not bare resolve) so the built-in per-event
    // default — window-morph for window snap events — shows as the current
    // value for an unset event. A user override (incl. an explicit "None")
    // still wins; the default is computed, never persisted.
    ShaderProfile resolved = resolveShaderWithDefault(tree, path);
    // Runtime-truth mirror of the compositor's applicability gate
    // (PlasmaZonesEffect::resolvedShaderAppliesToEvent), routed through the
    // same canonical predicate. A persisted effect that provably cannot
    // drive this event (a stale pre-split geometry pack on the drag leaf, a
    // hand-edited class mismatch) is refused at runtime, so displaying it
    // as "current" would advertise an animation that never plays — and the
    // class-filtered picker could not even show it as a selectable entry.
    // Blank only the VIEW: the persisted override is untouched and the
    // user's next pick overwrites it. An id the registry doesn't know
    // passes through, so a user pack that is still scanning doesn't flash
    // to "None" mid-warmup.
    const QString effectId = resolved.effectiveEffectId();
    if (!effectId.isEmpty() && m_shaderRegistry && m_shaderRegistry->hasEffect(effectId)
        && !shaderEffectAppliesToEventPath(m_shaderRegistry->effect(effectId), path)) {
        resolved.effectId.reset();
    }
    return shaderProfileToMap(resolved);
}

QStringList AnimationsPageController::stockSuppressedEvents() const
{
    using namespace PhosphorAnimationShaders;
    QStringList owned;
    if (!m_settings || !m_settings->animationsEnabled()) {
        return owned;
    }
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    // Mirrors the compositor's packOwnsEvent gate (syncStockEffectSuppression
    // in the kwin effect): tree-resolved effectId non-empty AND the pack
    // applies to the event's contract class. One deliberate divergence: an
    // effectId the registry does not know yet (a user pack still scanning at
    // startup) counts as owned here — resolvedShaderProfile grants unknown
    // ids the same warm-up grace, and flashing the conflict chip for a pack
    // that will suppress once scanned would be a false alarm.
    const auto packOwns = [this, &tree](const QString& path) {
        const ShaderProfile resolved = resolveShaderWithDefault(tree, path);
        const QString effectId = resolved.effectiveEffectId();
        if (effectId.isEmpty()) {
            return false;
        }
        if (m_shaderRegistry && m_shaderRegistry->hasEffect(effectId)
            && !shaderEffectAppliesToEventPath(m_shaderRegistry->effect(effectId), path)) {
            return false;
        }
        return true;
    };
    for (const QString& path :
         {PhosphorAnimation::ProfilePaths::WindowMinimize, PhosphorAnimation::ProfilePaths::WindowMaximize}) {
        if (packOwns(path)) {
            owned.append(path);
        }
    }
    return owned;
}

bool AnimationsPageController::setShaderOverride(const QString& path, const QString& effectId,
                                                 const QVariantMap& parameters)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return false;

    // Same race rationale as setOverride/clearOverride in
    // animationspagecontroller_overrides.cpp — the shader tree is
    // captured in m_pendingFileSnapshots when a discard starts, and
    // a concurrent mutation here would race the worker's
    // setShaderProfileTree write. The chrome gates the picker UI on
    // `discarding`; this guard protects programmatic callers.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "setShaderOverride: refusing write while async discard is in flight; path=" << path;
        return false;
    }

    // Cheap sanity check on effectId BEFORE the registry-membership gate
    // below — the registry gate is skipped during the "registry not yet
    // populated" startup window, so without this guard a stray Q_INVOKABLE
    // caller could smuggle a NUL byte, a path separator, or a multi-KB
    // string into the shader-profile tree where it would surface much
    // later as a corrupt resolve. 256 chars is well above any legitimate
    // effect id length and well below the cost of letting garbage through.
    if (!effectId.isEmpty()
        && (effectId.size() > 256 || effectId.contains(QLatin1Char('/')) || effectId.contains(QLatin1Char('\0')))) {
        qCWarning(lcConfig) << "setShaderOverride: rejecting effectId with illegal length/character; size="
                            << effectId.size();
        return false;
    }

    // Reject writes on paths the daemon's overlay service doesn't
    // consume as a shader-leg surface. Defence in depth: the QML UI
    // gates the picker via `supportsShaderLeg`, but a Q_INVOKABLE is
    // callable from anywhere (future scripts, tests, deserialisation
    // shims) and a stale tree entry on an unsupported path silently
    // shadows the user-intended parent override at runtime via the
    // resolver's deeper-leaf-wins overlay merge.
    if (!eventPathSupportsShaderLeg(path)) {
        qCWarning(lcConfig) << "setShaderOverride: path" << path
                            << "is not in shaderSupportedEventPaths(), ignoring (no daemon-side surface consumes it)";
        return false;
    }

    // Empty effectId writes an ENGAGED-EMPTY override at this path:
    // `ShaderProfile::effectId = std::optional<QString>("")`. This is
    // the "explicit no effect" sentinel — `ShaderProfile::overlay`
    // treats it as a real value that wins over a parent's effectId,
    // so inheritance from an ancestor (e.g. `popup` → "dissolve") is
    // BLOCKED at this path and every descendant resolves to no shader.
    // Parameters PASSED ALONGSIDE an empty effectId are preserved on
    // the disable sentinel — callers that need to clear them entirely
    // should pass `parameters = {}` (the default). This lets a future
    // re-enable inherit the previous tuning rather than starting over.
    //
    // This is intentionally distinct from `clearShaderOverride`, which
    // removes the override entry entirely so resolution falls through
    // to the parent. Without this distinction, an
    // AnimationEventCard's "Override OFF" toggle on `popup`
    // (cleared override) cannot stop the parent's dissolve from
    // cascading down to every popup event — exactly the user-reported
    // "I disabled all popups but dissolve still plays" bug. The
    // engaged-empty profile gives the UI a way to express "disable
    // shader at this path AND every descendant that doesn't override".
    if (effectId.isEmpty()) {
        ShaderProfile disabledProfile;
        disabledProfile.effectId = QString();
        if (!parameters.isEmpty())
            disabledProfile.parameters = parameters;
        ShaderProfileTree tree = m_settings->shaderProfileTree();
        // Compare-and-skip relies on `ShaderProfile::operator==` being
        // engaged-state-sensitive (forwards to `std::optional::operator==`,
        // which treats `nullopt` and `optional(empty)` as DISTINCT).
        // `disabledProfile` round-trips through toJson/fromJson without
        // changing engaged-state, so a disk-loaded disable sentinel for an
        // unchanged path short-circuits here. The construction above only
        // engages `disabledProfile.parameters` when the incoming map was
        // non-empty, so the on-disk round-trip form is always a match.
        if (tree.directOverride(path) == disabledProfile)
            return true;
        tree.setOverride(path, disabledProfile);
        m_settings->setShaderProfileTree(tree);
        // pendingChangesChanged is emitted by the shaderProfileTreeChanged
        // handler (the sole emitter for tree edits); dirtiness is value-based.
        return true;
    }

    // Reject unknown effect ids at the boundary — without this, a typo
    // from QML silently writes garbage into the shader-profile tree, and
    // the daemon's lookup at runtime returns nothing with no settings-side
    // diagnostic (the failure mode is "no shader applied, no error").
    //
    // The `effectIds().isEmpty()` guard avoids tripping the gate when the
    // registry hasn't yet scanned XDG dirs (asynchronous on some setups,
    // and unit tests construct an empty registry on purpose) — we can't
    // distinguish "id is unknown" from "registry not yet populated"
    // without a separate readiness signal.
    if (m_shaderRegistry && !m_shaderRegistry->effectIds().isEmpty() && !m_shaderRegistry->hasEffect(effectId)) {
        qCWarning(lcConfig) << "setShaderOverride: unknown effectId" << effectId << ", ignoring assignment for" << path;
        return false;
    }

    // Standard pattern: write through Settings::setShaderProfileTree.
    // The shaderProfileTreeJson Q_PROPERTY emits NOTIFY, the
    // SettingsController meta-object loop catches it. No per-edit
    // notify here, no snapshot, no custom dirty plumbing.
    ShaderProfile profile;
    profile.effectId = effectId;
    if (!parameters.isEmpty())
        profile.parameters = parameters;

    ShaderProfileTree tree = m_settings->shaderProfileTree();
    // Short-circuit when the tree is already at the requested state — avoids
    // a same-tree write that would cycle through Settings + the boomerang
    // and fire a spurious pendingChangesChanged.
    if (tree.directOverride(path) == profile)
        return true;
    tree.setOverride(path, profile);
    m_settings->setShaderProfileTree(tree);
    // pendingChangesChanged is emitted by the shaderProfileTreeChanged handler.
    return true;
}

bool AnimationsPageController::clearShaderOverride(const QString& path)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return false;
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "clearShaderOverride: refusing while async discard is in flight; path=" << path;
        return false;
    }
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    if (!tree.hasOverride(path))
        return false;
    tree.clearOverride(path);
    m_settings->setShaderProfileTree(tree);
    // pendingChangesChanged is emitted by the shaderProfileTreeChanged handler.
    return true;
}

int AnimationsPageController::shaderOverrideDescendantCount(const QString& path) const
{
    if (!m_settings)
        return 0;
    return collectShaderOverrideDescendants(m_settings->shaderProfileTree(), path).size();
}

int AnimationsPageController::clearShaderOverrideDescendants(const QString& path)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings)
        return 0;
    if (m_asyncRevertInFlight) {
        // -1, not 0: a caller must be able to tell "refused, try again" from
        // "there was nothing to clear" — the clearAllOverrides convention.
        qCWarning(lcConfig) << "clearShaderOverrideDescendants: refusing while async discard is in flight; path="
                            << path;
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot reset while a discard is in progress."));
        return -1;
    }
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    const QStringList toClear = collectShaderOverrideDescendants(tree, path);
    if (toClear.isEmpty())
        return 0;
    for (const QString& p : toClear)
        tree.clearOverride(p);
    m_settings->setShaderProfileTree(tree);
    // pendingChangesChanged is emitted by the shaderProfileTreeChanged handler.
    return toClear.size();
}

QVariantList AnimationsPageController::shaderEffectUsages(const QString& effectId) const
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || effectId.isEmpty())
        return {};
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    const QStringList overridden = tree.overriddenPaths();
    QVariantList out;
    for (const QString& p : overridden) {
        const ShaderProfile profile = tree.directOverride(p);
        if (!profile.effectId || *profile.effectId != effectId)
            continue;
        QVariantMap entry;
        entry.insert(QLatin1String("path"), p);
        entry.insert(QLatin1String("label"), eventLabel(p));
        out.append(entry);
    }
    // Sort by label for deterministic UI order across runs — the tree's
    // `overriddenPaths()` iterates a QHash internally so the order is
    // not stable.
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("label")).toString() < b.toMap().value(QLatin1String("label")).toString();
    });
    return out;
}

} // namespace PlasmaZones
