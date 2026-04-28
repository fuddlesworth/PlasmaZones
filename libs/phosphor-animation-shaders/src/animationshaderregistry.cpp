// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IScanStrategy.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphoranimationshaders.registry")
} // namespace

/// Scan strategy backing the registry's `WatchedDirectorySet`. Walks
/// subdirectories under each registered search path, parses
/// `metadata.json`, and reports the per-effect file paths the base must
/// re-arm individual watches on after each rescan.
///
/// Mirrors `PhosphorShell::ShaderRegistry::ShaderScanStrategy` — same
/// reverse-iterate / first-wins shape, same metadata.json convention. The
/// difference is the parse target (`AnimationShaderEffect` vs the zone-
/// shader `ShaderInfo`).
class AnimationShaderRegistry::EffectScanStrategy : public PhosphorFsLoader::IScanStrategy
{
public:
    explicit EffectScanStrategy(AnimationShaderRegistry& reg)
        : m_reg(&reg)
    {
    }

    QStringList performScan(const QStringList& directoriesInScanOrder) override
    {
        return m_reg->performScan(directoriesInScanOrder);
    }

private:
    AnimationShaderRegistry* m_reg;
};

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : QObject(parent)
    , m_strategy(std::make_unique<EffectScanStrategy>(*this))
    , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, this))
{
    // Connection deliberately omitted — `effectsChanged` is emitted from
    // inside `performScan` when the map diff is non-empty, NOT on every
    // `rescanCompleted`. Wiring through the base would emit on every
    // rescan regardless of content change, breaking the change-only
    // contract documented on the signal.
}

AnimationShaderRegistry::~AnimationShaderRegistry() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Search paths
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderRegistry::addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload)
{
    // Single-path: priority direction is irrelevant — forward with the
    // canonical default. The `addSearchPaths` overload's `order`
    // parameter only matters for multi-path batches.
    addSearchPaths(QStringList{path}, liveReload, PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
}

void AnimationShaderRegistry::addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload,
                                             PhosphorFsLoader::RegistrationOrder order)
{
    // Pre-canonicalise + drop already-registered paths via the shared
    // helper — keeps the log line below from spamming "Added search path:
    // /foo/bar/" when /foo/bar is already registered (the base's
    // `registerDirectories` is silent on dedup, so the filter has to
    // run upstream). Sister `PhosphorShell::ShaderRegistry` uses the
    // same helper for the same reason.
    const QStringList toRegister =
        PhosphorFsLoader::WatchedDirectorySet::filterNewSearchPaths(paths, m_watcher->directories());
    if (toRegister.isEmpty()) {
        return;
    }
    // Single batched register — one synchronous scan for the whole batch,
    // populates `m_effects` via the strategy, fires `effectsChanged`
    // exactly once if the diff is non-empty. Avoids the N-rescans-on-
    // startup amplification a loop of single-path registrations would
    // cause. The base normalises @p order into the canonical scan shape
    // before the strategy runs.
    m_watcher->registerDirectories(toRegister, liveReload, order);
    for (const QString& path : std::as_const(toRegister)) {
        qCInfo(lcRegistry) << "Added search path:" << path;
    }
}

void AnimationShaderRegistry::setUserShaderPath(const QString& path)
{
    if (m_userShaderPath == path) {
        return; // idempotent — same value, no work to do
    }
    // Canonicalisation happens at compare time in `performScan` (so
    // callers that pass a path which doesn't exist yet still get the
    // right classification once it materialises). Store the raw input.
    m_userShaderPath = path;
    // If search paths have already been registered, the prior scan baked
    // in the OLD user-path classification — a synchronous rescan
    // refreshes every effect's `isUserEffect` flag against the new value.
    // Without this, callers who set the user path AFTER `addSearchPaths`
    // would silently get every effect flagged as system until an explicit
    // `refresh()` ran.
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
    return m_effects.values();
}

AnimationShaderEffect AnimationShaderRegistry::effect(const QString& id) const
{
    return m_effects.value(id);
}

bool AnimationShaderRegistry::hasEffect(const QString& id) const
{
    return m_effects.contains(id);
}

QStringList AnimationShaderRegistry::effectIds() const
{
    return m_effects.keys();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Discovery
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderRegistry::refresh()
{
    // Synchronous rescan — `m_watcher->rescanNow()` calls into
    // `EffectScanStrategy::performScan` on this stack, which rebuilds
    // `m_effects` and emits `effectsChanged` if the map diff is non-
    // empty. Bypasses the debounce timer.
    qCDebug(lcRegistry) << "Refreshing animation shader registry";
    m_watcher->rescanNow();
}

QStringList AnimationShaderRegistry::performScan(const QStringList& directoriesInScanOrder)
{
    QHash<QString, AnimationShaderEffect> newEffects;
    QStringList desiredWatches;

    // Resolve the user-shader path's canonical form ONCE per rescan
    // (empty when no user path is configured or the configured path
    // doesn't exist yet). Each iterated dir is canonicalised below and
    // compared against this — match means the dir is the user path, and
    // discovered effects are flagged `isUserEffect = true`.
    const QString canonicalUserPath =
        m_userShaderPath.isEmpty() ? QString() : QFileInfo(m_userShaderPath).canonicalFilePath();

    // Reverse-iterate with first-registration-wins, matching the
    // IScanStrategy convention used by `JsonScanStrategy`,
    // `JsScanStrategy`, and `PhosphorShell::ShaderRegistry`. Caller
    // registers dirs in `[system-lowest, ..., system-highest, user]`
    // order; reversing here lets the user dir claim its effect IDs
    // before the system dirs are touched, which yields the canonical
    // XDG semantic `user > sys-highest > sys-mid > sys-lowest` on id
    // collision.
    for (auto dirIt = directoriesInScanOrder.crbegin(); dirIt != directoriesInScanOrder.crend(); ++dirIt) {
        const QString& searchPath = *dirIt;
        QDir dirObj(searchPath);
        if (!dirObj.exists()) {
            qCDebug(lcRegistry) << "Search path does not exist:" << searchPath;
            continue;
        }

        // Classify the iterated dir as user vs system. Empty
        // `canonicalUserPath` (no user path configured, or user dir
        // doesn't exist yet) yields `false` for every dir — preserving
        // the legacy default before this knob existed.
        const bool isUserDir =
            !canonicalUserPath.isEmpty() && QFileInfo(searchPath).canonicalFilePath() == canonicalUserPath;

        const QStringList subdirs = dirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& subdir : subdirs) {
            const QString effectDir = dirObj.filePath(subdir);
            const QString metadataPath = effectDir + QStringLiteral("/metadata.json");

            // Always re-arm the metadata.json watch even if parsing
            // fails or the id collides — an edit that fixes a broken
            // metadata.json is the most common way an effect transitions
            // from invisible to visible, so we want to wake on it.
            desiredWatches.append(metadataPath);

            // DoS guard: untrusted same-user metadata.json should not be
            // able to stall the GUI thread with a 2 GB blob. Reuse
            // `DirectoryLoader::kMaxFileBytes` as the SSOT — same cap
            // the sister `JsonScanStrategy` enforces on every JSON file
            // it loads.
            const QFileInfo metadataInfo(metadataPath);
            if (metadataInfo.exists() && metadataInfo.size() > PhosphorFsLoader::DirectoryLoader::kMaxFileBytes) {
                qCWarning(lcRegistry) << "Skipping oversized metadata.json:" << metadataPath << "("
                                      << metadataInfo.size() << "bytes, cap"
                                      << PhosphorFsLoader::DirectoryLoader::kMaxFileBytes << ")";
                continue;
            }

            QFile file(metadataPath);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }

            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                qCWarning(lcRegistry) << "Skipping" << metadataPath << ":" << parseError.errorString();
                continue;
            }
            if (!doc.isObject()) {
                qCWarning(lcRegistry) << "Skipping non-object root in" << metadataPath;
                continue;
            }

            AnimationShaderEffect e = AnimationShaderEffect::fromJson(doc.object());
            if (e.id.isEmpty()) {
                qCWarning(lcRegistry) << "Skipping" << metadataPath << ": missing 'id' field";
                continue;
            }

            // First-registration-wins. `performScan` reverse-iterates so
            // the user dir is processed first; a subsequent system effect
            // with a colliding id is shadowed and silently skipped here.
            if (newEffects.contains(e.id)) {
                qCDebug(lcRegistry) << "Effect id=" << e.id
                                    << "already registered from a higher-priority dir; shadowed at=" << effectDir;
                continue;
            }

            e.sourceDir = effectDir;
            e.isUserEffect = isUserDir;

            if (!e.fragmentShaderPath.isEmpty()) {
                e.fragmentShaderPath = effectDir + QLatin1Char('/') + e.fragmentShaderPath;
                desiredWatches.append(e.fragmentShaderPath);
            }
            if (!e.vertexShaderPath.isEmpty()) {
                e.vertexShaderPath = effectDir + QLatin1Char('/') + e.vertexShaderPath;
                desiredWatches.append(e.vertexShaderPath);
            }
            if (!e.kwinFragmentShaderPath.isEmpty()) {
                e.kwinFragmentShaderPath = effectDir + QLatin1Char('/') + e.kwinFragmentShaderPath;
                desiredWatches.append(e.kwinFragmentShaderPath);
            }
            if (!e.previewPath.isEmpty()) {
                e.previewPath = effectDir + QLatin1Char('/') + e.previewPath;
            }

            newEffects.insert(e.id, std::move(e));
        }
    }

    // Change-only emit — every rescan calls in here, but `effectsChanged`
    // only fires when the map diff is non-empty. Matches the legacy
    // behaviour of the bespoke registry's `if (newEffects != m_effects)`
    // guard that downstream tests (and the daemon's signal-driven
    // consumers) rely on.
    if (newEffects != m_effects) {
        m_effects = std::move(newEffects);
        Q_EMIT effectsChanged();
    }

    return desiredWatches;
}

} // namespace PhosphorAnimationShaders
