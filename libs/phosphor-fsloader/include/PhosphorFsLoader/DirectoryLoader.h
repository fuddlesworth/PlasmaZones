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
     * Idempotent on the registered SET, not on the work: adding a directory
     * already present does not duplicate it, but the call still runs a full
     * rescan and dispatches a `commitBatch` plus `entriesChanged`. A caller
     * that means "rescan now" should say `requestRescan()`; a caller that
     * means "make sure this is registered" pays a scan either way. Returns the
     * count of entries CURRENTLY registered after the scan (not the delta).
     *
     * `liveReload` is a one-way enable: once any call passes
     * `LiveReload::On`, the loader keeps watching for the rest of its
     * lifetime.
     *
     * The default is `Off` because this is a library primitive — tests
     * and batch imports compose against it. Consumer wrappers above
     * this (CurveLoader and ProfileLoader — the pack registries sit on
     * `MetadataPackLoader`, not on this class) inherit the default, but can override it via the explicit
     * `LiveReload::On` argument production callers pass.
     */
    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Register multiple directories in caller-declared priority order.
     *
     * `RegistrationOrder::LowestPriorityFirst` (the default) takes input
     * in `[sys-lowest, ..., sys-highest, user]` order — the shape a
     * consumer gets from `std::reverse(locateAll(...))` plus a user-dir
     * append. Passing `HighestPriorityFirst` lets callers feed
     * `locateAll`'s natural
     * output (with the user dir prepended) directly without their own
     * pre-reverse — the base normalises before the strategy runs, so
     * higher-priority entries always override on key collision.
     *
     * Same one-way `liveReload` semantics as `loadFromDirectory`.
     *
     * Empty `directories` is a no-op: no scan runs and the return value
     * is the count of entries currently tracked from PRIOR registrations
     * (not zero). Callers needing "force a rescan with no new dirs"
     * should use `requestRescan()`, or `rescanNow()` when the result has
     * to be readable before the call returns.
     */
    int loadFromDirectories(const QStringList& directories, LiveReload liveReload = LiveReload::Off,
                            RegistrationOrder order = RegistrationOrder::LowestPriorityFirst);

    /// Trigger a debounced rescan. Forwards to the underlying
    /// `WatchedDirectorySet`.
    void requestRescan();

    /// Rescan every registered directory synchronously, on the calling
    /// stack, cancelling any pending debounced rescan. Forwards to the
    /// underlying `WatchedDirectorySet`. For a consumer that has just
    /// written or deleted a file in a watched directory and must read
    /// the loaded state back before it returns: the debounce would
    /// otherwise answer that read with the pre-write state.
    ///
    /// GUI-thread only, like every other non-test mutating call on this
    /// class — debug-asserted in `WatchedDirectorySet`, not in release.
    /// `entriesChanged` is emitted on the caller's stack, so the sink's
    /// commit step and every DIRECTLY connected consumer slot run before
    /// this returns (a queued connection still runs later).
    ///
    /// Unlike `requestRescan`, this does not defer when a scan is already
    /// running: calling it from an `entriesChanged` slot re-enters the
    /// scan, and nothing bounds that recursion. The caller owns
    /// termination.
    void rescanNow();

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

    /// Hard cap on files CONSIDERED per rescan (summed across every
    /// registered directory), not on the entries that survive to
    /// registration — a file that fails to parse or loses a duplicate check
    /// has still been read. A file skipped for exceeding `kMaxFileBytes` is
    /// charged too: the stat is real work, and charging for it is the
    /// adversarially safer choice. At 10k files a rescan has already burned
    /// the 50 ms debounce budget many times over.
    ///
    /// What this does NOT bound is the `entryList` call that materialises
    /// and sorts every `*.json` name in a directory before the loop starts.
    /// That enumeration is kept small by `WatchedDirectorySet`'s
    /// forbidden-root check (no `$HOME`, no `/`) plus caller path
    /// discipline, not by this cap.
    ///
    /// Note the cost of a trip: keys registered on a previous scan that this
    /// one never reached are reported to the sink as `removedKeys`, so a
    /// trip unregisters entries that still exist on disk. At 10k that is
    /// theoretical, but it is why the cap sits far above any real corpus.
    /// NOTE this now bounds real kernel watches, not just parses: a file this
    /// scan REFUSES is added to the per-file watch list (so repairing it in
    /// place wakes the loader), and a refused file exists, so it takes an
    /// inotify watch. 10,000 unparseable `*.json` in one directory therefore
    /// holds 10,000 watches — above the 8192 `fs.inotify.max_user_watches`
    /// default several distributions still ship, which would starve every other
    /// QFileSystemWatcher in the process. `PluginLoader` sets its own cap an
    /// order of magnitude lower for exactly this reason.
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
    /// Fired after every rescan — regardless of whether the discovered
    /// entry set or any underlying payload actually changed. Rescans
    /// driven by the watcher or `requestRescan()` are coalesced by the
    /// 50 ms debounce first. `rescanNow()` bypasses that and fires this on
    /// the caller's stack, and so does the initial scan inside
    /// `loadFromDirectory[ies]` — which is why a consumer has to wire its
    /// slots BEFORE it registers directories, not after. (An empty directory
    /// list runs no scan and emits nothing; see `loadFromDirectories`.)
    ///
    /// This is **deliberately** a "rescan completed" signal rather than
    /// a "content changed" signal — the loader has no visibility into
    /// the sink's payload semantics, so it cannot diff at this layer.
    /// Sinks that need change-only consumer signals layer their own diff
    /// on top: `CurveLoader` and `ProfileLoader` each maintain a
    /// `lastBatchChanged` flag in their sink and gate `curvesChanged` /
    /// `profilesChanged` on it.
    ///
    /// Tests and debug tooling rely on the per-rescan emission to observe
    /// rescans without payload inspection — do not weaken this contract
    /// without updating the loader-sink consumers and the test suite.
    ///
    /// The three `MetadataPackLoader`-hosted pack registries (`ShaderRegistry`,
    /// `AnimationShaderRegistry`, `SurfaceShaderRegistry`) inherit change-only
    /// emit from BELOW them — `MetadataPackScanStrategy`'s SHA-1 signature plus
    /// `MetadataPackLoader`'s per-id fingerprint diff — and
    /// `ScriptedAlgorithmLoader` hashes its own registered set. This class has
    /// no view of parsed content, so its consumers gate one layer up.
    void entriesChanged();

private:
    class JsonScanStrategy;
    std::unique_ptr<JsonScanStrategy> m_strategy;
    std::unique_ptr<WatchedDirectorySet> m_watcher;
};

} // namespace PhosphorFsLoader
