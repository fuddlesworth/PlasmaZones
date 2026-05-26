// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Shader-leg methods for AnimationsPageController:
//   * Available-shader enumeration (availableShaderEffects,
//     shaderParameters, supportsShaderLeg).
//   * User shader directory + shader-pack install
//     (userShaderDirectoryPath, ensureUserShaderDirectory,
//     openUserShaderDirectory, installShaderPack).
//   * Per-event shader override (setShaderOverride, clearShaderOverride,
//     shaderOverrideDescendantCount, clearShaderOverrideDescendants,
//     shaderEffectUsages).
//
// Split out of animationspagecontroller.cpp to keep that file under
// the 800-line cap (see CLAUDE.md). All methods are members of
// PlasmaZones::AnimationsPageController and use its private state —
// same class, separate translation unit, no API change.

#include "animationspagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/utils.h"
#include "../pz_i18n.h"
#include "animationfileutils.h"

// IMPORTANT: include via the project-local path (PhosphorAnimation/),
// not the system PhosphorAnimationShaders/ path. There are two copies
// of this header in the dependency graph — the project's local one
// and a system-installed companion — and unity-build batches multiple
// TUs into one, so mixing the two paths produces "redefinition of
// struct" errors. The sibling animationspagecontroller.cpp picks the
// same path.
#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

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
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + ConfigDefaults::userAnimationsSubdir();
}

bool AnimationsPageController::ensureUserShaderDirectory()
{
    return QDir().mkpath(userShaderDirectoryPath());
}

void AnimationsPageController::openUserShaderDirectory()
{
    ensureUserShaderDirectory();
    QDesktopServices::openUrl(QUrl::fromLocalFile(userShaderDirectoryPath()));
}

namespace animations_controller_detail {

/// Copy a directory recursively. Qt has no built-in equivalent; the
/// stdlib's `std::filesystem::copy` exists but we stick to QDir/QFile
/// for consistency with the rest of the codebase. Returns false on the
/// first failure (broken file copy, mkpath fail, etc.) so the caller
/// can roll back via QDir::removeRecursively.
///
/// Symlinks (file or dir) are explicitly skipped via `QDir::NoSymLinks`
/// AND a per-entry `isSymLink()` guard. Without that, a dropped pack
/// containing `metadata.json -> /etc/shadow` or `assets -> /etc` would
/// silently follow the link during traversal and the recursive copy
/// would smuggle arbitrary readable filesystem content into the user
/// shader dir under deceptive names. A shader pack contains regular
/// files only; anything exotic is suspect and refused.
static bool copyDirRecursive(const QString& sourcePath, const QString& destPath)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists())
        return false;
    if (!QDir().mkpath(destPath))
        return false;

    const QFileInfoList entries =
        sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        // QDir::NoSymLinks already filters at the entryInfoList layer, but
        // recheck per-entry: filesystem races (the entry being replaced
        // by a symlink between enumeration and this iteration) and the
        // historical leniency of QDir::NoSymLinks across Qt versions
        // both argue for an explicit guard at the copy boundary.
        if (entry.isSymLink())
            continue;

        const QString destEntryPath = destPath + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirRecursive(entry.absoluteFilePath(), destEntryPath))
                return false;
        } else if (entry.isFile()) {
            // QFile::copy refuses to overwrite — caller's collision check
            // already guarantees a clean destination, but defend against
            // a partial failed previous run leaving stale files.
            if (QFile::exists(destEntryPath))
                QFile::remove(destEntryPath);
            if (!QFile::copy(entry.absoluteFilePath(), destEntryPath))
                return false;
        }
        // Devices, FIFOs, sockets, etc. are not isFile()/isDir() and
        // therefore fall through silently — same intent as the symlink
        // skip above.
    }
    return true;
}

} // namespace animations_controller_detail

bool AnimationsPageController::installShaderPack(const QString& sourceUrl)
{
    if (sourceUrl.isEmpty())
        return false;

    // Accept both `file://...` URLs (drag-drop from a file manager) and
    // bare paths (programmatic callers).
    QString sourcePath = sourceUrl;
    if (sourcePath.startsWith(QLatin1String("file://")))
        sourcePath = QUrl(sourceUrl).toLocalFile();

    // Normalise trailing slashes and `.`/`..` components — without this,
    // a drop URL like `file:///path/to/pack/` produces an empty
    // `fileName()` below and the destDir collapses onto the user shader
    // dir itself, surfacing as a confusing "destination already exists"
    // rather than a clean parse failure.
    sourcePath = QDir::cleanPath(sourcePath);

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir() || sourceInfo.isSymLink()) {
        qCWarning(lcConfig) << "installShaderPack: source is not an existing directory:" << sourcePath;
        return false;
    }
    const QString sourceBasename = sourceInfo.fileName();
    if (sourceBasename.isEmpty()) {
        qCWarning(lcConfig) << "installShaderPack: source path has no basename:" << sourcePath;
        return false;
    }

    // Validate metadata.json — without it the registry won't pick up the
    // pack, so accepting the drop would silently be a no-op. Reject
    // symlinked metadata so a malicious pack can't smuggle a non-shader
    // JSON file's content past the validation gate.
    const QString metadataPath = sourceInfo.absoluteFilePath() + QLatin1String("/metadata.json");
    const QFileInfo metadataInfo(metadataPath);
    if (!metadataInfo.exists() || !metadataInfo.isFile() || metadataInfo.isSymLink()) {
        qCWarning(lcConfig) << "installShaderPack: source has no metadata.json:" << sourcePath;
        return false;
    }

    if (!ensureUserShaderDirectory())
        return false;

    const QString destDir = userShaderDirectoryPath() + QLatin1Char('/') + sourceBasename;
    if (QFileInfo::exists(destDir)) {
        qCWarning(lcConfig) << "installShaderPack: destination already exists, refusing to overwrite:" << destDir;
        return false;
    }

    if (!animations_controller_detail::copyDirRecursive(sourceInfo.absoluteFilePath(), destDir)) {
        qCWarning(lcConfig) << "installShaderPack: copy failed; rolling back:" << destDir;
        QDir(destDir).removeRecursively();
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
    return shaderProfileToMap(tree.resolve(path));
}

bool AnimationsPageController::setShaderOverride(const QString& path, const QString& effectId,
                                                 const QVariantMap& parameters)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return false;

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
    m_settings->setShaderProfileTree(tree);
    m_shaderTreeDirty = true;
    Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::clearShaderOverride(const QString& path)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return false;
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    if (!tree.hasOverride(path))
        return false;
    tree.clearOverride(path);
    m_settings->setShaderProfileTree(tree);
    m_shaderTreeDirty = true;
    Q_EMIT pendingChangesChanged();
    return true;
}

namespace animations_controller_detail {
/// Collect every override path strictly DEEPER than @p path
/// (i.e. starting with `<path>.`). Centralises the prefix-match math
/// so shaderOverrideDescendantCount and clearShaderOverrideDescendants
/// share one definition of "descendant" — the trailing `.` boundary
/// is what excludes both the path itself ("popup") and unrelated
/// names with shared character-prefix ("popups").
static QStringList collectShaderOverrideDescendants(const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                    const QString& path)
{
    QStringList out;
    if (path.isEmpty()) {
        return out;
    }
    const QString prefix = path + QLatin1Char('.');
    const QStringList paths = tree.overriddenPaths();
    for (const QString& p : paths) {
        if (p.startsWith(prefix)) {
            out.append(p);
        }
    }
    return out;
}
} // namespace animations_controller_detail

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
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    const QStringList toClear = collectShaderOverrideDescendants(tree, path);
    if (toClear.isEmpty())
        return 0;
    for (const QString& p : toClear)
        tree.clearOverride(p);
    m_settings->setShaderProfileTree(tree);
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
