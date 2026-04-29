// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphoranimationshaders.registry")

/// Parse one already-validated metadata.json root into an
/// AnimationShaderEffect. The strategy already ran the file-existence,
/// size-cap, and JSON-object-root checks before invoking us; we own only
/// the schema-specific bits — directory-relative path resolution,
/// `isUserEffect` stamping, and minimum-viable validation.
///
/// Return `std::nullopt` to skip the entry silently. Returning an effect
/// with an empty `id` also skips it (the strategy checks).
std::optional<AnimationShaderEffect> parseEffect(const QString& effectDir, const QJsonObject& root, bool isUserDir)
{
    AnimationShaderEffect e = AnimationShaderEffect::fromJson(root);
    if (e.id.isEmpty()) {
        // The strategy logs the empty-id case at warning level — return
        // nullopt so we don't double-emit.
        return std::nullopt;
    }

    e.sourceDir = effectDir;
    e.isUserEffect = isUserDir;

    // Resolve directory-relative paths to absolute. The bare strings in
    // metadata.json are file names inside `effectDir`; consumers expect
    // absolute paths.
    if (!e.fragmentShaderPath.isEmpty()) {
        e.fragmentShaderPath = effectDir + QLatin1Char('/') + e.fragmentShaderPath;
    }
    if (!e.vertexShaderPath.isEmpty()) {
        e.vertexShaderPath = effectDir + QLatin1Char('/') + e.vertexShaderPath;
    }
    if (!e.previewPath.isEmpty()) {
        e.previewPath = effectDir + QLatin1Char('/') + e.previewPath;
    }

    return e;
}

/// Per-payload watch list — frag + vert shader file paths. The strategy
/// already adds the metadata.json itself; this callback is for everything
/// else. Preview is informational only (no live-reload need on a static
/// thumbnail) and is excluded.
QStringList effectWatchPaths(const AnimationShaderEffect& e)
{
    QStringList paths;
    if (!e.fragmentShaderPath.isEmpty()) {
        paths.append(e.fragmentShaderPath);
    }
    if (!e.vertexShaderPath.isEmpty()) {
        paths.append(e.vertexShaderPath);
    }
    return paths;
}

/// Hash the schema-specific bits change-detection cares about. The
/// strategy already mixes in `id`; this contributor adds path tuples,
/// the user/system flag, and source-dir so a rename or shadow shift
/// surfaces as a content change. Frag mtime+size aren't strictly needed
/// because per-file watches re-fire on edits — the next rescan will
/// already see the same bytes if nothing changed, and a content edit
/// changes the file's `lastModified` which the watcher already covers
/// at the wake layer. Keep the contributor light.
void contributeSignature(QCryptographicHash& h, const AnimationShaderEffect& e)
{
    h.addData(e.fragmentShaderPath.toUtf8());
    h.addData(QByteArrayView("|"));
    h.addData(e.vertexShaderPath.toUtf8());
    h.addData(QByteArrayView("|"));
    h.addData(e.sourceDir.toUtf8());
    h.addData(QByteArrayView("|"));
    h.addData(e.isUserEffect ? "u" : "s");
}

} // namespace

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : QObject(parent)
    , m_strategy(std::make_unique<ScanStrategy>(parseEffect,
                                                [this]() {
                                                    Q_EMIT effectsChanged();
                                                }))
    , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, this))
{
    m_strategy->setPerEntryWatchPaths(effectWatchPaths);
    m_strategy->setSignatureContrib(contributeSignature);
    m_strategy->setLoggingCategory(&lcRegistry());
}

AnimationShaderRegistry::~AnimationShaderRegistry() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Search paths
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderRegistry::addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload)
{
    // Single-path: priority direction is irrelevant — forward with the
    // canonical default.
    addSearchPaths(QStringList{path}, liveReload, PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
}

void AnimationShaderRegistry::addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload,
                                             PhosphorFsLoader::RegistrationOrder order)
{
    // Pre-canonicalise + drop already-registered paths — keeps the log
    // line below from spamming "Added search path: /foo/bar/" when
    // /foo/bar is already registered (the base's `registerDirectories`
    // is silent on dedup, so the filter has to run upstream).
    const QStringList toRegister =
        PhosphorFsLoader::WatchedDirectorySet::filterNewSearchPaths(paths, m_watcher->directories());
    if (toRegister.isEmpty()) {
        return;
    }
    m_watcher->registerDirectories(toRegister, liveReload, order);
    for (const QString& path : std::as_const(toRegister)) {
        qCInfo(lcRegistry) << "Added search path:" << path;
    }
}

void AnimationShaderRegistry::setUserShaderPath(const QString& path)
{
    if (m_userShaderPath == path) {
        return; // idempotent
    }
    m_userShaderPath = path;
    m_strategy->setUserPath(path);
    // If search paths have already been registered, the prior scan baked
    // in the OLD user-path classification — synchronous rescan refreshes
    // every effect's `isUserEffect` flag against the new value.
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->rescanNow();
    }
}

QStringList AnimationShaderRegistry::searchPaths() const
{
    return m_watcher->directories();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

QList<AnimationShaderEffect> AnimationShaderRegistry::availableEffects() const
{
    // Strategy returns a sorted-by-id snapshot — single source of truth
    // for QHash-randomisation-stable output.
    return m_strategy->packs();
}

AnimationShaderEffect AnimationShaderRegistry::effect(const QString& id) const
{
    return m_strategy->pack(id);
}

bool AnimationShaderRegistry::hasEffect(const QString& id) const
{
    return m_strategy->contains(id);
}

QStringList AnimationShaderRegistry::effectIds() const
{
    QStringList ids = m_strategy->packsById().keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Discovery
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderRegistry::refresh()
{
    qCDebug(lcRegistry) << "Refreshing animation shader registry";
    m_watcher->rescanNow();
}

} // namespace PhosphorAnimationShaders
