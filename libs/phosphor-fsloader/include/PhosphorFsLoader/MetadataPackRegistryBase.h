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
 * Owns BOTH the strategy and the `WatchedDirectorySet` and provides the
 * search-path management surface (`addSearchPath`, `addSearchPaths`,
 * `searchPaths`, `setUserPath`, `refresh`) that every consumer of
 * `MetadataPackScanStrategy<Payload>` was hand-rolling identically.
 * Subclasses pass an already-configured strategy into the base ctor and
 * keep a typed raw-pointer alias for their own setter / accessor needs;
 * they override `onUserPathChanged` to forward the new path into it.
 *
 * ## Single-phase init + ownership-driven destruction order
 *
 * The strategy is owned by the base via `std::unique_ptr<IScanStrategy>`
 * passed into the ctor. `m_strategy` is declared BEFORE `m_watcher` so
 * the watcher (which holds a borrowed reference to the strategy) is
 * destroyed first — without this ordering, the watcher's reference would
 * dangle during base-member teardown if the subclass also held the
 * strategy. Subclasses keep a non-owning typed pointer, populated from
 * `strategy()` in the subclass member-init list, for their own setter
 * calls.
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
    Q_DISABLE_COPY_MOVE(MetadataPackRegistryBase)

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
     * @brief Construct the base, taking ownership of @p strategy.
     *
     * The subclass builds + configures its strategy (parser, watch
     * extractors, signature contributor, logging category), then hands
     * ownership in. Constructing the watcher happens here too — by the
     * time the ctor returns, the registry is fully initialised and ready
     * to accept search-path calls.
     *
     * @param logCat   Category for the "Added search path" / refresh log
     *                 lines and any future base-level diagnostics.
     *                 Stored by reference; must outlive the registry (a
     *                 `Q_LOGGING_CATEGORY`-defined static is the standard
     *                 source).
     * @param strategy The configured scan strategy. Must be non-null;
     *                 ownership transfers to the base. The base
     *                 guarantees the strategy outlives the watcher
     *                 because `m_strategy` is declared before `m_watcher`
     *                 in the private section (reverse-order destruction).
     */
    MetadataPackRegistryBase(const QLoggingCategory& logCat, std::unique_ptr<IScanStrategy> strategy,
                             QObject* parent = nullptr);

    /**
     * @brief Hook invoked from `setUserPath` when the path actually
     *        changes. Subclass forwards the new value into the typed
     *        strategy (`m_strategy->setUserPath(path)`). The base then
     *        triggers a rescan if any directories are registered.
     */
    virtual void onUserPathChanged(const QString& path) = 0;

    /// Strategy accessor for subclasses. Returned as `IScanStrategy*` so
    /// the base stays free of the consumer's `Payload` template parameter;
    /// the subclass `static_cast`s back to its concrete strategy type.
    /// Always non-null (the ctor enforces non-null at construction).
    IScanStrategy* strategy() const
    {
        return m_strategy.get();
    }

    /// Watcher accessor for subclasses that need direct ops not covered
    /// by the base API (e.g. a `setDirectories` replacement). Always
    /// non-null after construction.
    WatchedDirectorySet* watcher() const
    {
        return m_watcher.get();
    }

private:
    // Declaration order is load-bearing for destruction safety: members
    // are torn down in reverse declaration order, so `m_watcher` (which
    // holds a borrowed reference into `*m_strategy`) MUST be declared
    // AFTER `m_strategy`. Without this ordering, the strategy reference
    // inside the watcher would dangle during base teardown.
    std::unique_ptr<IScanStrategy> m_strategy;
    std::unique_ptr<WatchedDirectorySet> m_watcher;
    QString m_userPath;
    const QLoggingCategory* m_logCat;
};

} // namespace PhosphorFsLoader
