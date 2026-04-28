// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/IDirectoryLoaderSink.h>
#include <PhosphorFsLoader/ParsedEntry.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>
#include <PhosphorFsLoader/phosphorfsloader_export.h>

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

namespace PhosphorFsLoader {

/**
 * @brief Generic JSON-directory loader with debounced live reload.
 *
 * A thin specialisation of `WatchedDirectorySet` that scans top-level
 * `*.json` in every registered directory, applies user-wins layering on
 * key collision (caller passes dirs system-first / user-last per the
 * `loadFromDirectories` docstring), and dispatches per-file parsing +
 * batch commits to a consumer-supplied `IDirectoryLoaderSink`.
 *
 * Watching, debouncing, parent-watch promotion, and rescan-during-rescan
 * race handling all live in the underlying `WatchedDirectorySet` —
 * `DirectoryLoader` adds only the JSON-specific concerns:
 *
 *   • Top-level `*.json` filtering.
 *   • Per-file size cap (`kMaxFileBytes`) and per-rescan entry cap
 *     (`kMaxEntries`) DoS guards.
 *   • Sink dispatch (`parseFile` per file, one `commitBatch` per scan).
 *   • Stale-entry purge: deleted files' keys are reported to the sink
 *     as `removedKeys` so the sink can unregister them.
 *
 * Loaders that need a different on-disk shape (subdirectory layouts,
 * non-JSON file extensions, custom filename validation) implement
 * `IScanStrategy` directly against `WatchedDirectorySet` rather than
 * extending this class.
 *
 * ## Thread safety
 *
 * GUI-thread only. Inherits the threading constraint from
 * `WatchedDirectorySet`.
 */
class PHOSPHORFSLOADER_EXPORT DirectoryLoader : public QObject
{
    Q_OBJECT

public:
    /// Tracked entry — mirrors `ParsedEntry` minus the payload.
    /// Exposed via `entries()` for tests and debug introspection.
    struct Entry
    {
        QString key;
        QString sourcePath;
        QString systemSourcePath;
    };

    /**
     * @brief Construct with a borrowed sink.
     *
     * `sink` must outlive the loader. Taken by reference rather than
     * raw pointer so there is no need for a null-check at the call
     * site — "sink is always valid" is a compile-time guarantee.
     */
    explicit DirectoryLoader(IDirectoryLoaderSink& sink, QObject* parent = nullptr);
    ~DirectoryLoader() override;

    DirectoryLoader(const DirectoryLoader&) = delete;
    DirectoryLoader& operator=(const DirectoryLoader&) = delete;

    /**
     * @brief Register a directory for scanning + (optionally) watching.
     *
     * Idempotent on the directory path — adding the same directory
     * twice is a no-op on the second call. Returns the count of
     * entries CURRENTLY registered after the scan (not the delta).
     *
     * `liveReload` is a one-way enable: once any call passes
     * `LiveReload::On`, the loader keeps watching for the rest of its
     * lifetime.
     *
     * The default is `Off` because this is a library primitive — tests
     * and batch imports compose against it. Consumer wrappers above
     * this (CurveLoader/ProfileLoader and the shader registries)
     * inherit the default, but can override it via the explicit
     * `LiveReload::On` argument production callers pass.
     */
    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Register multiple directories in caller-declared priority order.
     *
     * `RegistrationOrder::LowestPriorityFirst` (the default) takes input
     * in `[sys-lowest, ..., sys-highest, user]` order — the same shape
     * the daemon's `setupAnimationProfiles` already builds via
     * `std::reverse(locateAll(...))` + user-dir append. Passing
     * `HighestPriorityFirst` lets callers feed `locateAll`'s natural
     * output (with the user dir prepended) directly without their own
     * pre-reverse — the base normalises before the strategy runs, so
     * higher-priority entries always override on key collision.
     *
     * Same one-way `liveReload` semantics as `loadFromDirectory`.
     *
     * Empty `directories` is a no-op: no scan runs and the return value
     * is the count of entries currently tracked from PRIOR registrations
     * (not zero). Callers needing "force a rescan with no new dirs"
     * should use `requestRescan()` instead.
     */
    int loadFromDirectories(const QStringList& directories, LiveReload liveReload = LiveReload::Off,
                            RegistrationOrder order = RegistrationOrder::LowestPriorityFirst);

    /// Trigger a debounced rescan. Forwards to the underlying
    /// `WatchedDirectorySet`.
    void requestRescan();

    /// Count of entries currently tracked by the loader.
    int registeredCount() const;

    /// Current entries (post last rescan), sorted by `key` for
    /// deterministic order across platforms and Qt versions.
    QList<Entry> entries() const;

    /// Per-file size cap. Files larger than this are skipped with a
    /// warning — guards the GUI thread against a pathological user
    /// JSON. 1 MiB is far above any legitimate curve / profile / layout
    /// schema in this library's ecosystem.
    static constexpr qint64 kMaxFileBytes = 1 * 1024 * 1024;

    /// Hard cap on entries parsed per rescan (summed across every
    /// registered directory). At 10k entries a rescan has already burned
    /// the 50 ms debounce budget many times over.
    static constexpr int kMaxEntries = 10'000;

    /// Test-only: override the debounce interval (default 50 ms).
    void setDebounceIntervalForTest(int ms);

    /// Test-only: override the per-rescan entry cap (default
    /// `kMaxEntries`). Lets the cap regression test trip the guard with
    /// 3-digit file counts rather than having to materialise 10k files.
    void setMaxEntriesForTest(int cap);

    /// Test-only: ancestor-watch introspection forwarded to the
    /// underlying `WatchedDirectorySet`.
    QString watchedAncestorForTest(const QString& target) const;

    /// Test-only: parent-watch introspection forwarded to the
    /// underlying `WatchedDirectorySet`.
    bool hasParentWatchForTest(const QString& path) const;

Q_SIGNALS:
    /// Fired after every rescan, coalesced by the 50 ms debounce.
    void entriesChanged();

private:
    class JsonScanStrategy;
    std::unique_ptr<JsonScanStrategy> m_strategy;
    std::unique_ptr<WatchedDirectorySet> m_watcher;
};

} // namespace PhosphorFsLoader
