// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationShaders/AnimationShaderEffect.h>
#include <PhosphorAnimationShaders/phosphoranimationshaders_export.h>

#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace PhosphorAnimationShaders {

/**
 * @brief Registry of available animation shader transition effects.
 *
 * Discovers shader packs from configured search paths. Each pack is a
 * subdirectory containing a `metadata.json` describing the effect plus
 * the shader source files it references.
 *
 * ## Directory layout
 *
 * ```
 * <search-path>/
 *   dissolve/
 *     metadata.json    ← { "id": "dissolve", "fragmentShader": "effect.frag", ... }
 *     effect.frag
 *     effect_kwin.frag
 *   slide/
 *     metadata.json
 *     effect.frag
 * ```
 *
 * The subdirectory name is decorative — the `id` field in metadata.json
 * is the registry key. This matches `PhosphorShaders::ShaderRegistry`'s
 * convention for zone shaders.
 *
 * ## Search path ordering
 *
 * Caller registers paths in `[system-lowest, ..., system-highest, user]`
 * order; the strategy reverse-iterates to apply first-registration-wins,
 * yielding the canonical XDG semantic
 * `user > sys-highest > sys-mid > sys-lowest` on id collision. Same
 * convention as `PhosphorShaders::ShaderRegistry` and the in-tree
 * `JsonScanStrategy` / `JsScanStrategy`.
 *
 * ## Live reload
 *
 * Backed by `PhosphorFsLoader::WatchedDirectorySet`. The first
 * `addSearchPath[s]` call with `LiveReload::On` (the default) installs a
 * `QFileSystemWatcher` with the standard 50 ms debounce, parent-watch
 * promotion for missing user-data dirs (fresh-install hot-reload), per-
 * file watches re-armed on every rescan (atomic-rename inode-swap
 * recovery), and rescan-during-rescan race protection. Forbidden roots
 * (`$HOME`, `/`, XDG data/config/cache/temp/runtime, Documents,
 * Downloads) are refused — registering a target whose parent climbs onto
 * one of those silently degrades to "no live reload until consumer
 * `refresh()`" instead of carpet-rescanning the user's home directory.
 *
 * ## Thread safety
 *
 * GUI-thread only for both reads and mutations. `m_effects` is rebuilt
 * on the GUI thread inside the rescan; the public lookup methods
 * (`availableEffects`, `effect`, `hasEffect`, `effectIds`) read it
 * without synchronisation, so concurrent worker-thread reads are racy.
 *
 * `searchPaths()` is the one exception: it returns a by-value snapshot
 * of an implicitly-shared QStringList, so a GUI-thread caller can
 * snapshot it and propagate the result to worker threads (matches the
 * shader-warming path's contract). Calling `searchPaths()` *from* a
 * worker thread concurrently with a GUI-thread mutation is a data race;
 * snapshot on the GUI thread first.
 */
class PHOSPHORANIMATIONSHADERS_EXPORT AnimationShaderRegistry : public QObject
{
    Q_OBJECT

public:
    explicit AnimationShaderRegistry(QObject* parent = nullptr);
    ~AnimationShaderRegistry() override;

    // ── Search paths ──────────────────────────────────────────────────

    /// Add a single search-path directory. Forwards to the batched form
    /// so the underlying watcher only runs one initial scan; prefer
    /// `addSearchPaths` directly when registering more than one path
    /// during construction (avoids N redundant scans, one per path).
    ///
    /// @p liveReload defaults to `On` so production callers get
    /// hot-reload by default. Pass `Off` from tests / batch-import
    /// contexts that want a one-shot scan with no background watcher.
    /// Inherits the underlying `WatchedDirectorySet`'s set-wide one-way-
    /// enable semantics: once any call passes `On`, the watcher stays
    /// armed for the registry's lifetime.
    void addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On);

    /// Add multiple search-path directories in one shot. The strategy
    /// applies first-registration-wins on id collision under the
    /// canonical convention; @p order tells the base which end of @p paths
    /// is highest-priority so it can normalise before the strategy runs.
    /// Default `LowestPriorityFirst` matches `[sys-lowest, ..., sys-highest,
    /// user]` (what the daemon's `setupAnimationShaderEffects` already
    /// builds); pass `HighestPriorityFirst` to feed `locateAll`'s natural
    /// output without a manual pre-reverse.
    ///
    /// Prefer this over a loop of `addSearchPath` calls — a single
    /// batched call runs exactly one scan instead of N.
    ///
    /// Same `liveReload` semantics as the single-path overload.
    void addSearchPaths(
        const QStringList& paths, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);

    /// Mark @p path as the "user" search path for `AnimationShaderEffect::isUserEffect`
    /// classification. Stored as-given; the rescan canonicalises both this
    /// path and each iterated search dir before comparing, so the input
    /// can be either canonical or symlinked. Pass the empty string (the
    /// default) to disable user/system differentiation — every effect will
    /// then report `isUserEffect == false`.
    ///
    /// Order-independent: callable before OR after `addSearchPaths`. When
    /// the value changes and at least one search path is already
    /// registered, the call triggers a synchronous rescan so already-
    /// discovered effects get reclassified immediately. Idempotent:
    /// passing the same value twice is a no-op.
    ///
    /// GUI-thread only — when the path changes, the synchronous rescan
    /// asserts the calling thread.
    void setUserShaderPath(const QString& path);

    /// Currently-registered search paths in registration order. Forwards
    /// to the underlying `WatchedDirectorySet::directories()` — single
    /// source of truth, no parallel member that could drift out of sync.
    /// Safe to call from any thread (see class docs).
    QStringList searchPaths() const;

    // ── Lookup ────────────────────────────────────────────────────────

    QList<AnimationShaderEffect> availableEffects() const;
    AnimationShaderEffect effect(const QString& id) const;
    bool hasEffect(const QString& id) const;
    QStringList effectIds() const;

    // ── Lifecycle ─────────────────────────────────────────────────────

    /// Synchronous rescan — re-walks every search path on the calling
    /// stack, replaces `m_effects`, and emits `effectsChanged` if the
    /// post-rescan map differs from the pre-rescan map. Use after a
    /// caller-mediated filesystem change that the watcher can't see
    /// (e.g. a programmatic effect install via a separate process).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void effectsChanged();

private:
    class EffectScanStrategy;
    QStringList performScan(const QStringList& directoriesInScanOrder);

    QHash<QString, AnimationShaderEffect> m_effects;
    /// User-shader search path used to classify discovered effects as
    /// user vs system. Compared against each iterated search dir's
    /// canonical form on every rescan — see `setUserShaderPath`.
    QString m_userShaderPath;
    std::unique_ptr<EffectScanStrategy> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
};

} // namespace PhosphorAnimationShaders
