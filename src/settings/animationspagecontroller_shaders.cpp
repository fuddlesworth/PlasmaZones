// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Shader-leg methods for AnimationsPageController:
//   * Available-shader enumeration (availableShaderEffects,
//     availableShaderEffectsForPath, shaderEffectInfo, shaderParameters,
//     supportsShaderLeg).
//   * User shader directory + shader-pack install
//     (userShaderDirectoryPath, ensureUserShaderDirectory,
//     openUserShaderDirectory, installShaderPack).
//   * Per-event shader read + override (rawShaderProfile,
//     resolvedShaderProfile, setShaderOverride, clearShaderOverride,
//     shaderOverrideDescendantCount, clearShaderOverrideDescendants,
//     shaderEffectUsages).
//
// Split out of animationspagecontroller.cpp to keep that file under
// the 800-line cap (see CLAUDE.md). All methods are members of
// PlasmaZones::AnimationsPageController and use its private state —
// same class, separate translation unit, no API change.

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

namespace {
/// RAII scope guard for AnimationsPageController::m_mutatingShaderTree.
/// Increments the depth on construction, decrements on destruction —
/// including the exception path through setShaderProfileTree, where a
/// bare set/clear pair would leave the depth stuck >0 and silently
/// disable external-reload dirty clearance for the rest of the process.
/// Counter (not flag): the NOTIFY-driven external-reload handler's
/// guard checks `depth > 0`, so a nested re-entrant write (inner scope
/// destructs first) does NOT prematurely clear the outer's protection.
struct MutatingShaderTreeScope
{
    int& depth;
    explicit MutatingShaderTreeScope(int& d)
        : depth(d)
    {
        ++depth;
    }
    ~MutatingShaderTreeScope()
    {
        --depth;
    }
    MutatingShaderTreeScope(const MutatingShaderTreeScope&) = delete;
    MutatingShaderTreeScope& operator=(const MutatingShaderTreeScope&) = delete;
};

/// Human-readable tooltip for an effect that can't drive an event row of
/// class @p pathClass. Only called when a mismatch is proven, so @p
/// pathClass is always a concrete class and the effect supports the OTHER
/// one. The two messages name the events the effect DOES apply to so the
/// user knows where to use it instead.
QString shaderPathMismatchReason(const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                                 const QString& pathClass)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    if (pathClass == PP::EventClassAppearance) {
        // Effect is geometry-only (e.g. window-morph) on an appearance row.
        return PhosphorI18n::tr(
                   "“%1” needs an old and new geometry — it only applies to move, resize, snap and tile events.")
            .arg(effect.name);
    }
    // pathClass == geometry: effect is appearance-only on a geometry row.
    return PhosphorI18n::tr(
               "“%1” animates a surface fading in or out — it has no effect on move, resize, snap or tile events.")
        .arg(effect.name);
}
} // namespace

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

    // Class of this event row — drives the dim reason. Computed once; empty
    // for an ambiguous row, in which case nothing dims (the predicate
    // returns true for every effect).
    const QString pathClass = PhosphorAnimation::ProfilePaths::eventClassForPath(path);

    const auto effects = m_shaderRegistry->availableEffects();
    result.reserve(effects.size());
    for (const auto& effect : effects) {
        QVariantMap m = effectToMap(effect);
        const bool compatible = PhosphorAnimationShaders::shaderEffectAppliesToEventPath(effect, path);
        m.insert(QLatin1String("dimmed"), !compatible);
        m.insert(QLatin1String("dimReason"), compatible ? QString() : shaderPathMismatchReason(effect, pathClass));
        result.append(m);
    }
    return result;
}

QVariantMap AnimationsPageController::shaderEffectInfo(const QString& effectId) const
{
    if (!m_shaderRegistry || effectId.isEmpty() || !m_shaderRegistry->hasEffect(effectId))
        return {};
    return effectToMap(m_shaderRegistry->effect(effectId));
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

bool AnimationsPageController::ensureUserShaderDirectory()
{
    return QDir().mkpath(userShaderDirectoryPath());
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
    // default — window-morph for window-move events — shows as the current
    // value for an unset event. A user override (incl. an explicit "None")
    // still wins; the default is computed, never persisted.
    return shaderProfileToMap(resolveShaderWithDefault(tree, path));
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
    // so inheritance from an ancestor (e.g. `panel` → "dissolve") is
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
        {
            MutatingShaderTreeScope guard(m_mutatingShaderTree);
            m_settings->setShaderProfileTree(tree);
        }
        m_shaderTreeDirty = true;
        Q_EMIT pendingChangesChanged();
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
    {
        MutatingShaderTreeScope guard(m_mutatingShaderTree);
        m_settings->setShaderProfileTree(tree);
    }
    m_shaderTreeDirty = true;
    Q_EMIT pendingChangesChanged();
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
    {
        MutatingShaderTreeScope guard(m_mutatingShaderTree);
        m_settings->setShaderProfileTree(tree);
    }
    m_shaderTreeDirty = true;
    Q_EMIT pendingChangesChanged();
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
        qCWarning(lcConfig) << "clearShaderOverrideDescendants: refusing while async discard is in flight; path="
                            << path;
        return 0;
    }
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    const QStringList toClear = collectShaderOverrideDescendants(tree, path);
    if (toClear.isEmpty())
        return 0;
    for (const QString& p : toClear)
        tree.clearOverride(p);
    {
        MutatingShaderTreeScope guard(m_mutatingShaderTree);
        m_settings->setShaderProfileTree(tree);
    }
    m_shaderTreeDirty = true;
    Q_EMIT pendingChangesChanged();
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
