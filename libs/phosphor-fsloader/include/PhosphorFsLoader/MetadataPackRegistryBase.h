// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/WatchedDirectorySet.h>
#include <PhosphorFsLoader/phosphorfsloader_export.h>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

QT_BEGIN_NAMESPACE
class QLoggingCategory;
QT_END_NAMESPACE

namespace PhosphorFsLoader {

class IScanStrategy;

/**
 * @brief QObject base for registries hosting a `MetadataPackScanStrategy<Payload>`.
 *
 * Owns the `WatchedDirectorySet` and provides the search-path management
 * surface (`addSearchPath`, `addSearchPaths`, `searchPaths`, `setUserPath`,
 * `refresh`) that every consumer of `MetadataPackScanStrategy<Payload>`
 * was hand-rolling identically. Subclasses own their typed strategy and
 * override `onUserPathChanged` to forward the new path into it.
 *
 * ## Two-phase init
 *
 * Construction is split into ctor + `initWatcher` so the subclass can
 * build its typed strategy on its own member-init list (the strategy's
 * concrete `Payload` type is invisible to this base) and hand it to the
 * watcher inside the ctor body. `initWatcher` MUST be called exactly
 * once before any search-path call; assertion-checked in debug builds.
 *
 * ## Method naming
 *
 * `setUserPath` matches `MetadataPackScanStrategy<Payload>::setUserPath`
 * — the base, the strategy, and the public API all spell the concept
 * the same way. The base is domain-neutral (lives in `phosphor-fsloader`)
 * so domain-specific names like "user shader path" don't belong here.
 *
 * ## Thread safety
 *
 * GUI-thread only — inherits the underlying `WatchedDirectorySet`'s
 * threading constraint. `searchPaths()` returns a by-value snapshot
 * suitable for forwarding to worker threads after retrieval, but every
 * mutating call (`addSearchPath`, `setUserPath`, `refresh`) must happen
 * on the construction thread.
 */
class PHOSPHORFSLOADER_EXPORT MetadataPackRegistryBase : public QObject
{
    Q_OBJECT

public:
    ~MetadataPackRegistryBase() override;

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
    void addSearchPath(const QString& path, LiveReload liveReload = LiveReload::On);

    /// Add multiple search-path directories in one shot. The strategy
    /// applies first-registration-wins on id collision under the
    /// canonical convention; @p order tells the base which end of @p paths
    /// is highest-priority so it can normalise before the strategy runs.
    /// Default `LowestPriorityFirst` matches `[sys-lowest, ..., sys-highest,
    /// user]` (what daemon-side setup typically builds); pass
    /// `HighestPriorityFirst` to feed `locateAll`'s natural output without
    /// a manual pre-reverse.
    ///
    /// Prefer this over a loop of `addSearchPath` calls — a single
    /// batched call runs exactly one scan instead of N.
    ///
    /// Same `liveReload` semantics as the single-path overload.
    void addSearchPaths(const QStringList& paths, LiveReload liveReload = LiveReload::On,
                        RegistrationOrder order = RegistrationOrder::LowestPriorityFirst);

    /// Currently-registered search paths in registration order. Forwards
    /// to the underlying `WatchedDirectorySet::directories()`.
    QStringList searchPaths() const;

    /// Mark @p path as the user-data root for `isUser` classification on
    /// discovered packs. Stored as-given; the strategy canonicalises both
    /// this path and each iterated search dir before comparing, so the
    /// input can be either canonical or symlinked. Pass the empty string
    /// (the default) to disable user/system differentiation — every pack
    /// will then be classified as system.
    ///
    /// Order-independent: callable before OR after `addSearchPaths`. When
    /// the value changes and at least one search path is already
    /// registered, the call triggers a synchronous rescan so already-
    /// discovered packs get reclassified immediately. Idempotent: passing
    /// the same value twice is a no-op. Bidirectional: passing the empty
    /// string clears the user-path designation, passing a non-empty
    /// string sets or replaces it.
    ///
    /// GUI-thread only — when the path changes, the synchronous rescan
    /// asserts the calling thread.
    void setUserPath(const QString& path);

    /// Synchronous rescan — re-walks every search path on the calling
    /// stack, replaces the strategy's pack map, and fires the consumer's
    /// content-changed signal if the strategy reports a signature change.
    /// Use after a caller-mediated filesystem change that the watcher
    /// can't see (e.g. a programmatic install via a separate process).
    Q_INVOKABLE void refresh();

protected:
    /**
     * @brief Construct the base. Subclass MUST call `initWatcher` from
     *        its own ctor body before exposing the registry to callers.
     *
     * @param logCat  Category for the "Added search path" / refresh log
     *                lines and any future base-level diagnostics. Stored
     *                by reference; must outlive the registry (a
     *                `Q_LOGGING_CATEGORY`-defined static is the standard
     *                source).
     */
    explicit MetadataPackRegistryBase(const QLoggingCategory& logCat, QObject* parent = nullptr);

    /**
     * @brief Two-phase init: bind the watcher to the subclass-owned
     *        strategy. Must be called exactly once from the subclass
     *        ctor body, after the strategy is constructed.
     *
     * @param strategy  The typed strategy stored on the subclass. Must
     *                  outlive the watcher (i.e. live as long as this
     *                  registry); the subclass guarantees this by
     *                  declaring the strategy member before invoking
     *                  the base ctor.
     */
    void initWatcher(IScanStrategy& strategy);

    /**
     * @brief Hook invoked from `setUserPath` when the path actually
     *        changes. Subclass forwards the new value into the typed
     *        strategy (`m_strategy->setUserPath(path)`). The base then
     *        triggers a rescan if any directories are registered.
     */
    virtual void onUserPathChanged(const QString& path) = 0;

    /// Watcher accessor for subclasses that need direct ops not covered
    /// by the base API (e.g. a `setDirectories` replacement). Returns
    /// nullptr before `initWatcher` runs.
    WatchedDirectorySet* watcher() const
    {
        return m_watcher.get();
    }

private:
    std::unique_ptr<WatchedDirectorySet> m_watcher;
    QString m_userPath;
    const QLoggingCategory* m_logCat;
};

} // namespace PhosphorFsLoader
